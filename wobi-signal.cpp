#include "wobi-signal.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include "ExecutionTypes.h"
#include "FillInfo.h"
#include "Order.h"
#include "OrderParams.h"

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
      m_num_levels(5),         // default n
      m_entry_threshold(0.0),  // default t (you'll tune this)
      m_exit_threshold(0.0),   // default exit (I < 0)
      m_persistence_len(3),    // default l
      m_weight_exponent(1.0),  // default w
      m_latency_ns(0.0),       // default a
      m_position_size(100),
      m_debug_on(true) {}

WobiSignalStrategy::~WobiSignalStrategy() {}

/*===========================================================
 *   Reset Strategy State
 *===========================================================*/

void WobiSignalStrategy::OnResetStrategyState() {
    m_persistence_map.clear();
    m_last_imbalance.clear();
    m_instrument_order_id_map.clear();
}

/*===========================================================
 *   Event Registration
 *===========================================================*/

void WobiSignalStrategy::RegisterForStrategyEvents(
    StrategyEventRegister* eventRegister, DateType currDate) {
    (void)currDate;

    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForMarketData(*it);
        eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, 1);
    }
}

/*===========================================================
 *   Define Strategy Parameters
 *===========================================================*/

void WobiSignalStrategy::DefineStrategyParams() {
    // n: number of price levels to analyze
    CreateStrategyParamArgs arg1("num_levels", STRATEGY_PARAM_TYPE_STARTUP,
                                 VALUE_TYPE_INT, m_num_levels);
    params().CreateParam(arg1);

    // t: imbalance threshold for entry (I > t)
    CreateStrategyParamArgs arg2("entry_threshold", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE, m_entry_threshold);
    params().CreateParam(arg2);

    // exit threshold (often 0; when imbalance reverses out of ideal range)
    CreateStrategyParamArgs arg3("exit_threshold", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE, m_exit_threshold);
    params().CreateParam(arg3);

    // l: persistence length in ticks
    CreateStrategyParamArgs arg4("persistence_len", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_INT, m_persistence_len);
    params().CreateParam(arg4);

    // w: weighting exponent for level weights ( (i+1)^w )
    CreateStrategyParamArgs arg5("weight_exponent", STRATEGY_PARAM_TYPE_STARTUP,
                                 VALUE_TYPE_DOUBLE, m_weight_exponent);
    params().CreateParam(arg5);

    // a: round-trip latency assumption (ns)
    CreateStrategyParamArgs arg6("latency_ns", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_DOUBLE, m_latency_ns);
    params().CreateParam(arg6);

    // position size (shares)
    CreateStrategyParamArgs arg7("position_size", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_INT, m_position_size);
    params().CreateParam(arg7);

    // debug logging flag
    CreateStrategyParamArgs arg8("debug", STRATEGY_PARAM_TYPE_RUNTIME,
                                 VALUE_TYPE_BOOL, m_debug_on);
    params().CreateParam(arg8);
}

/*===========================================================
 *   Define Strategy Commands
 *===========================================================*/

void WobiSignalStrategy::DefineStrategyCommands() {
    StrategyCommand command1(1, "Cancel All Orders");
    commands().AddCommand(command1);

    // You can add more commands later (e.g., "Flatten All Positions")
}

/*===========================================================
 *   Event Handlers
 *===========================================================*/


void WobiSignalStrategy::OnDepth(const MarketDepthEventMsg& msg) {
    const Instrument& inst = msg.instrument();

    // if (m_debug_on) {
    //     const MarketModels::IAggrOrderBook& book = inst.aggregate_order_book();
    //     cout << "[DEPTH] (" << msg.adapter_time() << ") " << inst.symbol();

    //     // Log top 3 levels
    //     for (int i = 0; i < 3; ++i) {
    //         const MarketModels::IAggrPriceLevel* bid_lvl =
    //             book.BidPriceLevelAtLevel(i);
    //         const MarketModels::IAggrPriceLevel* ask_lvl =
    //             book.AskPriceLevelAtLevel(i);

    //         if (bid_lvl) {
    //             cout << " | B" << i << "=" << std::fixed << std::setprecision(2)
    //                  << bid_lvl->price() << "(" << bid_lvl->size() << ")";
    //         }
    //         if (ask_lvl) {
    //             cout << " | A" << i << "=" << std::fixed << std::setprecision(2)
    //                  << ask_lvl->price() << "(" << ask_lvl->size() << ")";
    //         }
    //     }
    //     cout << endl;
    // }

    double imbalance = ComputeWeightedImbalance(inst);
    EvaluateImbalanceSignal(inst, imbalance, msg.adapter_time());
}

/*===========================================================
 *   Order Updates
 *===========================================================*/

void WobiSignalStrategy::OnOrderUpdate(const OrderUpdateEventMsg& msg) {
    const Order& order = msg.order();
    const Instrument* inst = order.instrument();

    // Log order state changes
    // cout << "[OnOrderUpdate] " << msg.update_time()
    //      << " | Symbol=" << inst->symbol() << " | OrderID=" << order.order_id()
    //      << " | State=" << OrderStateToString(order.order_state())
    //      << " | UpdateType=" << OrderUpdateTypeToString(msg.update_type())
    //      << endl;

    // Log execution details when fills occur
    if (msg.fill_occurred()) {
        const FillInfo* fill = msg.fill();
        if (fill) {
            std::string side_str =
                IsBuySide(order.order_side()) ? "BUY" : "SELL";

            cout << "[EXECUTION] " << fill->fill_time()
                 << " | ACTION=" << side_str << " | Symbol=" << inst->symbol()
                 << " | Price=" << std::fixed << std::setprecision(2)
                 << fill->price() << " | Size=" << fill->size()
                 << " | OrderID=" << order.order_id()
                 << " | Partial=" << (fill->is_partial() ? "YES" : "NO")
                 << endl;
        }
    }

    if (msg.completes_order()) {
        // Clear order ID tracking for this instrument
        m_instrument_order_id_map[inst] = 0;

        cout << "[OnOrderUpdate] Order complete for " << inst->symbol()
             << " | FinalState=" << OrderStateToString(order.order_state())
             << " | FilledQty=" << order.size_completed() << endl;
    }
}

/*===========================================================
 *   Strategy Commands
 *===========================================================*/

void WobiSignalStrategy::OnStrategyCommand(const StrategyCommandEventMsg& msg) {
    switch (msg.command_id()) {
        case 1:
            // Cancel all working orders
            trade_actions()->SendCancelAll();
            if (m_debug_on) {
                cout << "[OnStrategyCommand] Cancel All Orders" << endl;
            }
            break;
        default:
            logger().LogToClient(LOGLEVEL_DEBUG,
                                 "Unknown strategy command received");
            break;
    }
}

/*===========================================================
 *   Parameter Changed
 *===========================================================*/

void WobiSignalStrategy::OnParamChanged(StrategyParam& param) {
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

double WobiSignalStrategy::ComputeWeightedImbalance(
    const Instrument& inst) const {
    const MarketModels::IAggrOrderBook& book = inst.aggregate_order_book();

    // If book not initialized or missing depth, stay neutral.
    if (book.is_initializing()) {
        return 0.0;
    }

    const int levels = std::max(1, m_num_levels);

    double weighted_bids = 0.0;
    double weighted_asks = 0.0;
    double weighted_total = 0.0;

    for (int i = 0; i < levels; ++i) {
        const MarketModels::IAggrPriceLevel* bid_lvl =
            book.BidPriceLevelAtLevel(i);
        const MarketModels::IAggrPriceLevel* ask_lvl =
            book.AskPriceLevelAtLevel(i);

        // Weighting formula from proposal: w_i = 1/(i+1)^w
        // Level 0 (top of book) gets highest weight (1.0 when w=1)
        const double w =
            1.0 / std::pow(static_cast<double>(i + 1), m_weight_exponent);

        const int bid_sz = bid_lvl ? bid_lvl->size() : 0;
        const int ask_sz = ask_lvl ? ask_lvl->size() : 0;

        weighted_bids += w * static_cast<double>(bid_sz);
        weighted_asks += w * static_cast<double>(ask_sz);
        weighted_total += w * static_cast<double>(bid_sz + ask_sz);
    }

    if (weighted_total == 0.0) {
        return 0.0;
    }

    // Symmetric NULL handling: if either side is NULL, treat both as 0 (imbalance
    // = 0)
    const MarketModels::IAggrPriceLevel* bid_lvl_0 =
        book.BidPriceLevelAtLevel(0);
    const MarketModels::IAggrPriceLevel* ask_lvl_0 =
        book.AskPriceLevelAtLevel(0);
    if (!bid_lvl_0 || !ask_lvl_0) {
        return 0.0;
    }

    return (weighted_bids - weighted_asks) / weighted_total;
}

void WobiSignalStrategy::EvaluateImbalanceSignal(const Instrument& inst,
                                                 double imbalance,
                                                 TimeType event_time) {
    const Instrument* inst_ptr = &inst;

    m_last_imbalance[inst_ptr] = imbalance;

    // Ensure persistence map has entry for this instrument
    if (m_persistence_map.find(inst_ptr) == m_persistence_map.end()) {
        m_persistence_map[inst_ptr] = 0;
    }

    // Use portfolio as the single source of truth for position
    int current_position = portfolio().position(inst_ptr);
    bool in_position = (current_position > 0);

    // Always log imbalance summary for diagnostics
    // cout << "[IMBALANCE] " << inst.symbol() << " | t=" << event_time
    //      << " | I=" << std::fixed << std::setprecision(4) << imbalance
    //      << " | threshold=" << m_entry_threshold
    //      << " | persistence=" << m_persistence_map[inst_ptr]
    //      << " | position=" << current_position
    //      << " | status=" << (in_position ? "LONG" : "FLAT") << endl;

    // ENTRY RULE (from proposal):
    //   If NOT in a position (position == 0) and I > t for l consecutive ticks →
    //   BUY.
    if (!in_position) {
        if (imbalance > m_entry_threshold) {
            m_persistence_map[inst_ptr] += 1;

            if (m_persistence_map[inst_ptr] >= m_persistence_len) {
                cout << "\n*** BUY SIGNAL ***" << endl;
                cout << "[SIGNAL] " << event_time << " | ACTION=BUY"
                     << " | SYMBOL=" << inst.symbol()
                     << " | SIZE=" << m_position_size
                     << " | IMBALANCE=" << std::fixed << std::setprecision(4)
                     << imbalance
                     << " | PERSISTENCE=" << m_persistence_map[inst_ptr]
                     << " | THRESHOLD=" << m_entry_threshold << endl;
                cout << "*** BUY SIGNAL ***\n" << endl;

                EnterLong(inst);
                m_persistence_map[inst_ptr] = 0;  // reset after entering
            }
        } else {
            // Reset persistence if signal breaks.
            m_persistence_map[inst_ptr] = 0;
        }
    }
    // EXIT RULE (from proposal):
    //   If IN a position (position > 0) and I falls below exit_threshold (e.g.,
    //   0) → SELL.
    else {
        // Log if we would have triggered a buy but are blocked by existing position
        if (imbalance > m_entry_threshold) {
            m_persistence_map[inst_ptr] += 1;
            if (m_persistence_map[inst_ptr] >= m_persistence_len) {
                // cout << "[BLOCKED_BUY] " << inst.symbol()
                //      << " | Position=" << current_position
                //      << " | Cannot buy while holding shares"
                //      << " | I=" << std::fixed << std::setprecision(4)
                //      << imbalance << endl;
                m_persistence_map[inst_ptr] = 0;
            }
        } else {
            m_persistence_map[inst_ptr] = 0;
        }

        // Check for sell signal
        if (imbalance < m_exit_threshold) {
            cout << "\n*** SELL SIGNAL ***" << endl;
            cout << "[SIGNAL] " << event_time << " | ACTION=SELL"
                 << " | SYMBOL=" << inst.symbol()
                 << " | SIZE=" << current_position  // Sell entire position
                 << " | IMBALANCE=" << std::fixed << std::setprecision(4)
                 << imbalance << " | EXIT_THRESHOLD=" << m_exit_threshold
                 << endl;
            cout << "*** SELL SIGNAL ***\n" << endl;

            ExitLong(inst);
            m_persistence_map[inst_ptr] = 0;
        }
    }
}

/*===========================================================
 *   Order Helpers
 *===========================================================*/

void WobiSignalStrategy::EnterLong(const Instrument& inst) {
    // Get expected fill price (best ask for buys)
    double expected_price = inst.top_quote().ask();

    cout << "[ORDER] ENTERING LONG POSITION"
         << " | Symbol=" << inst.symbol() << " | Size=" << m_position_size
         << " | Side=BUY"
         << " | TIF=FOK"
         << " | ExpectedPrice=" << std::fixed << std::setprecision(2)
         << expected_price << " | LatencyNs=" << m_latency_ns << endl;

    // Create order params for market buy with Fill-or-Kill
    OrderParams params(
        inst, m_position_size,
        0.0,                   // price (not used for market orders)
        MARKET_CENTER_ID_IEX,  // default market center
        ORDER_SIDE_BUY,
        ORDER_TIF_FOK,  // Fill or Kill - all shares must fill or cancel
        ORDER_TYPE_MARKET);

    TradeActionResult result = trade_actions()->SendNewOrder(params);

    if (result == TRADE_ACTION_RESULT_SUCCESSFUL) {
        m_instrument_order_id_map[&inst] = params.order_id;
        cout << "[ORDER] BUY order sent successfully | OrderID="
             << params.order_id << endl;
    } else {
        cout << "[ORDER] BUY order FAILED | Result=" << result << endl;
    }
}

void WobiSignalStrategy::ExitLong(const Instrument& inst) {
    // Get expected fill price (best bid for sells)
    double expected_price = inst.top_quote().bid();

    cout << "[ORDER] EXITING LONG POSITION"
         << " | Symbol=" << inst.symbol() << " | Size=" << m_position_size
         << " | Side=SELL"
         << " | TIF=GTC"
         << " | ExpectedPrice=" << std::fixed << std::setprecision(2)
         << expected_price << " | LatencyNs=" << m_latency_ns << endl;

    // Create order params for market sell with Good-Till-Cancelled
    OrderParams params(
        inst, m_position_size,
        0.0,                   // price (not used for market orders)
        MARKET_CENTER_ID_IEX,  // default market center
        ORDER_SIDE_SELL,
        ORDER_TIF_GTC,  // Good Till Cancelled - stays until filled
        ORDER_TYPE_MARKET);

    TradeActionResult result = trade_actions()->SendNewOrder(params);

    if (result == TRADE_ACTION_RESULT_SUCCESSFUL) {
        m_instrument_order_id_map[&inst] = params.order_id;
        cout << "[ORDER] SELL order sent successfully | OrderID="
             << params.order_id << endl;
    } else {
        cout << "[ORDER] SELL order FAILED | Result=" << result << endl;
    }
}
