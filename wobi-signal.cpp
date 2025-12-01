#ifdef _WIN32
    #include "stdafx.h"
#endif

#include "wobi-signal.h"

#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"

#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <iostream>
#include <cassert>

using namespace RCM::StrategyStudio;
using namespace RCM::StrategyStudio::MarketModels;
using namespace RCM::StrategyStudio::Utilities;

using namespace std;

/*===========================================================
 *   Constructor / Destructor
 *===========================================================*/

WobiSignalStrategy::WobiSignalStrategy(StrategyID strategyID,
                                       const std::string& strategyName,
                                       const std::string& groupName)
    : Strategy(strategyID, strategyName, groupName),
      m_num_levels(5),          // default n
      m_entry_threshold(0.0),   // default t (you'll tune this)
      m_exit_threshold(0.0),    // default exit (I < 0)
      m_persistence_len(3),     // default l
      m_weight_exponent(1.0),   // default w
      m_latency_ns(0.0),        // default a
      m_position_size(100),
      m_debug_on(true)
{
}

WobiSignalStrategy::~WobiSignalStrategy()
{
}

/*===========================================================
 *   Reset Strategy State
 *===========================================================*/

void WobiSignalStrategy::OnResetStrategyState()
{
    m_last_trade_price.clear();
    m_position_map.clear();
    m_persistence_map.clear();
    m_last_imbalance_map.clear();
    m_instrument_order_id_map.clear();
}

/*===========================================================
 *   Event Registration
 *===========================================================*/

void WobiSignalStrategy::RegisterForStrategyEvents(StrategyEventRegister* eventRegister,
                                                   DateType currDate)
{
    UNREFERENCED_PARAMETER(currDate);

    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it)
    {
        // Bars can be useful for PnL or summary stats.
        eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, 1);

        // Core data sources for this strategy:
        eventRegister->RegisterForDepth(*it);      // multi-level order book
        eventRegister->RegisterForTrade(*it);      // trade prints
        eventRegister->RegisterForTopQuote(*it);   // NBBO/top-of-book
        eventRegister->RegisterForQuote(*it);      // market center quotes (optional)
    }
}

/*===========================================================
 *   Define Strategy Parameters
 *===========================================================*/

void WobiSignalStrategy::DefineStrategyParams()
{
    // n: number of price levels to analyze
    CreateStrategyParamArgs arg1("num_levels",
                                 STRATEGY_PARAM_TYPE_STARTUP,
                                 VALUE_TYPE_INT,
                                 m_num_levels);
    params().CreateParam(arg1);

    // t: imbalance threshold for entry (I > t)
    CreateStrategyParamArgs arg2("entry_threshold",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE,
                                 m_entry_threshold);
    params().CreateParam(arg2);

    // exit threshold (often 0; when imbalance reverses out of ideal range)
    CreateStrategyParamArgs arg3("exit_threshold",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE,
                                 m_exit_threshold);
    params().CreateParam(arg3);

    // l: persistence length in ticks
    CreateStrategyParamArgs arg4("persistence_len",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_INT,
                                 m_persistence_len);
    params().CreateParam(arg4);

    // w: weighting exponent for level weights ( (i+1)^w )
    CreateStrategyParamArgs arg5("weight_exponent",
                                 STRATEGY_PARAM_TYPE_STARTUP,
                                 VALUE_TYPE_DOUBLE,
                                 m_weight_exponent);
    params().CreateParam(arg5);

    // a: round-trip latency assumption (ns)
    CreateStrategyParamArgs arg6("latency_ns",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE,
                                 m_latency_ns);
    params().CreateParam(arg6);

    // position size (shares)
    CreateStrategyParamArgs arg7("position_size",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_INT,
                                 m_position_size);
    params().CreateParam(arg7);

    // debug logging flag
    CreateStrategyParamArgs arg8("debug",
                                 STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_BOOL,
                                 m_debug_on);
    params().CreateParam(arg8);
}

/*===========================================================
 *   Define Strategy Commands
 *===========================================================*/

void WobiSignalStrategy::DefineStrategyCommands()
{
    StrategyCommand command1(1, "Cancel All Orders");
    commands().AddCommand(command1);

    // You can add more commands later (e.g., "Flatten All Positions")
}

/*===========================================================
 *   Event Handlers
 *===========================================================*/

void WobiSignalStrategy::OnTrade(const TradeDataEventMsg& msg)
{
    // Track last trade price by symbol (useful for PnL or sanity checks)
    m_last_trade_price[msg.instrument().symbol()] = msg.trade().price();

    if (m_debug_on) {
        cout << "[OnTrade] (" << msg.adapter_time()
             << ") " << msg.instrument().symbol()
             << " size=" << msg.trade().size()
             << " @ " << msg.trade().price()
             << endl;
    }
}

void WobiSignalStrategy::OnTopQuote(const QuoteEventMsg& msg)
{
    if (m_debug_on) {
        cout << "[OnTopQuote] (" << msg.adapter_time()
             << ") " << msg.instrument().symbol()
             << " bid=" << msg.instrument().top_quote().bid_size()
             << "@" << msg.instrument().top_quote().bid()
             << " ask=" << msg.instrument().top_quote().ask_size()
             << "@" << msg.instrument().top_quote().ask()
             << endl;
    }
}

void WobiSignalStrategy::OnQuote(const QuoteEventMsg& msg)
{
    // Optional: per-market-center quote logging.
    // Left intentionally light to avoid spamming output.
    UNREFERENCED_PARAMETER(msg);
}

void WobiSignalStrategy::OnDepth(const MarketDepthEventMsg& msg)
{
    // This is the core event for your order book imbalance logic.
    EvaluateImbalanceSignal(msg);
}

void WobiSignalStrategy::OnBar(const BarEventMsg& msg)
{
    // Optional: per-bar stats, logging, PnL summaries, etc.
    UNREFERENCED_PARAMETER(msg);
}

/*===========================================================
 *   Order Updates
 *===========================================================*/

void WobiSignalStrategy::OnOrderUpdate(const OrderUpdateEventMsg& msg)
{
    if (m_debug_on) {
        cout << "[OnOrderUpdate] " << msg.update_time()
             << " name=" << msg.name() << endl;
    }

    if (msg.completes_order()) {
        // Track completion per instrument if desired
        const Instrument& inst = msg.order().instrument();
        m_instrument_order_id_map[&inst] = 0;

        if (m_debug_on) {
            cout << "[OnOrderUpdate] order complete for "
                 << inst.symbol() << endl;
        }
    }
}

/*===========================================================
 *   Strategy Commands
 *===========================================================*/

void WobiSignalStrategy::OnStrategyCommand(const StrategyCommandEventMsg& msg)
{
    switch (msg.command_id()) {
        case 1:
            // Cancel all working orders
            trade_actions()->SendCancelAll();
            if (m_debug_on) {
                cout << "[OnStrategyCommand] Cancel All Orders" << endl;
            }
            break;
        default:
            logger().LogToClient(LOGLEVEL_DEBUG, "Unknown strategy command received");
            break;
    }
}

/*===========================================================
 *   Parameter Changed
 *===========================================================*/

void WobiSignalStrategy::OnParamChanged(StrategyParam& param)
{
    // Mirroring the DiaIndexArb style, but actually applied:

    if (param.param_name() == "num_levels") {
        if (!param.Get(&m_num_levels))
            throw StrategyStudioException("Could not get num_levels");
    } else if (param.param_name() == "entry_threshold") {
        if (!param.Get(&m_entry_threshold))
            throw StrategyStudioException("Could not get entry_threshold");
    } else if (param.param_name() == "exit_threshold") {
        if (!param.Get(&m_exit_threshold))
            throw StrategyStudioException("Could not get exit_threshold");
    } else if (param.param_name() == "persistence_len") {
        if (!param.Get(&m_persistence_len))
            throw StrategyStudioException("Could not get persistence_len");
    } else if (param.param_name() == "weight_exponent") {
        if (!param.Get(&m_weight_exponent))
            throw StrategyStudioException("Could not get weight_exponent");
    } else if (param.param_name() == "latency_ns") {
        if (!param.Get(&m_latency_ns))
            throw StrategyStudioException("Could not get latency_ns");
    } else if (param.param_name() == "position_size") {
        if (!param.Get(&m_position_size))
            throw StrategyStudioException("Could not get position_size");
    } else if (param.param_name() == "debug") {
        if (!param.Get(&m_debug_on))
            throw StrategyStudioException("Could not get debug flag");
    }
}

/*===========================================================
 *   Imbalance Logic (Core of Project)
 *===========================================================*/

double WobiSignalStrategy::ComputeWeightedImbalance(const MarketDepthEventMsg& msg)
{
    // TODO: Replace this stub with real multi-level book logic.
    //
    // Concept from proposal:
    //  - Look at n price levels away from top of book on both sides.
    //  - Define weights w_i = (i+1)^w for levels i = 0..n-1
    //  - Compute a weighted imbalance I, e.g. something like:
    //
    //      I = ( sum_i w_i * BidSize_i - sum_i w_i * AskSize_i ) 
    //          / ( sum_i w_i * (BidSize_i + AskSize_i) )
    //
    //  - I > t (entry_threshold) is bullish -> consider buy
    //  - I < exit_threshold (often 0) -> exit the position
    //
    // Your parser / usage of Strategy Studio's depth book API
    // will go here, using msg to access the depth.

    UNREFERENCED_PARAMETER(msg);

    // Returning 0.0 keeps everything safe until the real logic is wired in.
    return 0.0;
}

void WobiSignalStrategy::EvaluateImbalanceSignal(const MarketDepthEventMsg& msg)
{
    const Instrument& inst = msg.instrument();
    const Instrument* inst_ptr = &inst;

    double imbalance = ComputeWeightedImbalance(msg);
    m_last_imbalance_map[inst_ptr] = imbalance;

    // Ensure maps have entries for this instrument
    if (m_position_map.find(inst_ptr) == m_position_map.end()) {
        m_position_map[inst_ptr] = WOBI_SIDE_FLAT;
    }
    if (m_persistence_map.find(inst_ptr) == m_persistence_map.end()) {
        m_persistence_map[inst_ptr] = 0;
    }

    WobiPositionSide side = m_position_map[inst_ptr];
    bool in_position = (side != WOBI_SIDE_FLAT);

    if (m_debug_on) {
        cout << "[EvaluateImbalanceSignal] " << inst.symbol()
             << " I=" << imbalance
             << " in_pos=" << in_position
             << " persistence=" << m_persistence_map[inst_ptr]
             << endl;
    }

    // ENTRY RULE (from proposal):
    //   If NOT in a position and I > t for l consecutive ticks → BUY.
    if (!in_position) {
        if (imbalance > m_entry_threshold) {
            m_persistence_map[inst_ptr] += 1;

            if (m_persistence_map[inst_ptr] >= m_persistence_len) {
                EnterLong(inst);
                m_position_map[inst_ptr] = WOBI_SIDE_LONG;
                m_persistence_map[inst_ptr] = 0; // reset after entering
            }
        } else {
            // Reset persistence if signal breaks.
            m_persistence_map[inst_ptr] = 0;
        }
    }
    // EXIT RULE (from proposal):
    //   If IN a position and I falls below exit_threshold (e.g., 0) → SELL.
    else {
        if (imbalance < m_exit_threshold) {
            ExitLong(inst);
            m_position_map[inst_ptr] = WOBI_SIDE_FLAT;
            m_persistence_map[inst_ptr] = 0;
        }
    }
}

/*===========================================================
 *   Order Helpers
 *===========================================================*/

void WobiSignalStrategy::EnterLong(const Instrument& inst)
{
    if (m_debug_on) {
        cout << "[EnterLong] BUY " << m_position_size
             << " of " << inst.symbol() << endl;
    }

    // When you are ready to send real orders, uncomment and
    // ensure the signature matches Strategy Studio version:
    //
    // OrderID id = trade_actions()->SendNewMarketOrder(
    //     inst,
    //     ORDER_SIDE_BUY,
    //     m_position_size);
    //
    // m_instrument_order_id_map[&inst] = id;
}

void WobiSignalStrategy::ExitLong(const Instrument& inst)
{
    if (m_debug_on) {
        cout << "[ExitLong] SELL " << m_position_size
             << " of " << inst.symbol() << endl;
    }

    // Likewise, uncomment when ready:
    //
    // OrderID id = trade_actions()->SendNewMarketOrder(
    //     inst,
    //     ORDER_SIDE_SELL,
    //     m_position_size);
    //
    // m_instrument_order_id_map[&inst] = id;
}
