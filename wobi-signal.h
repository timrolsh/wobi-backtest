#pragma once

#ifndef _STRATEGY_STUDIO_LIB_EXAMPLES_WOBI_SIGNAL_STRATEGY_H_
#define _STRATEGY_STUDIO_LIB_EXAMPLES_WOBI_SIGNAL_STRATEGY_H_

#ifdef _WIN32
#define _STRATEGY_EXPORTS __declspec(dllexport)
#else
#ifndef _STRATEGY_EXPORTS
#define _STRATEGY_EXPORTS
#endif
#endif

#include <MarketDepthEventMsg.h>
#include <MarketModels/IAggrOrderBook.h>
#include <MarketModels/IAggrPriceLevel.h>
#include <MarketModels/Instrument.h>
#include <Strategy.h>
#include <Utilities/ParseConfig.h>

#include <boost/unordered_map.hpp>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

/**
 * WobiSignalStrategy
 *
 * Multilevel Order Book Imbalance Momentum strategy for FIN 556.
 *
 * It listens to MarketDepthEventMsg, uses the aggregate order book,
 * then computes a weighted imbalance:
 *
 *   w_i = 1 / (i+1)^w   (level 0 = top of book gets highest weight)
 *   I   = sum_i w_i (BidSize_i - AskSize_i) / sum_i w_i (BidSize_i + AskSize_i)
 *
 * Trading rules:
 *   - Buy (FOK): when portfolio position == 0 AND I > entry_threshold for persistence_len ticks.
 *   - Sell (GTC): when portfolio position > 0 AND I < exit_threshold.
 *   - Position gating: uses portfolio() as single source of truth.
 */
class WobiSignalStrategy : public RCM::StrategyStudio::Strategy {
   public:
    typedef boost::unordered_map<
        const RCM::StrategyStudio::MarketModels::Instrument*, int>
        PersistenceMap;
    typedef boost::unordered_map<
        const RCM::StrategyStudio::MarketModels::Instrument*, double>
        ImbalanceMap;
    typedef boost::unordered_map<std::string, double> TradePriceMap;

   public:
    WobiSignalStrategy(RCM::StrategyStudio::StrategyID strategyID,
                       const std::string& strategyName,
                       const std::string& groupName);
    virtual ~WobiSignalStrategy();

    //
    // IEventCallback interface
    //
   public:
    // virtual void OnTrade(const RCM::StrategyStudio::TradeDataEventMsg& msg);
    // virtual void OnTopQuote(const RCM::StrategyStudio::QuoteEventMsg& msg);
    // virtual void OnQuote(const RCM::StrategyStudio::QuoteEventMsg& msg);
    virtual void OnDepth(const RCM::StrategyStudio::MarketDepthEventMsg& msg);
    // virtual void OnBar(const RCM::StrategyStudio::BarEventMsg& msg);

    // virtual void OnMarketState(
    //     const RCM::StrategyStudio::MarketStateEventMsg& msg) {};
    virtual void OnOrderUpdate(
        const RCM::StrategyStudio::OrderUpdateEventMsg& msg);
    // virtual void OnStrategyControl(
    //     const RCM::StrategyStudio::StrategyStateControlEventMsg& msg) {};
    virtual void OnResetStrategyState();
    // virtual void OnDataSubscription(
    //     const RCM::StrategyStudio::DataSubscriptionEventMsg& msg) {};
    virtual void OnStrategyCommand(
        const RCM::StrategyStudio::StrategyCommandEventMsg& msg);
    virtual void OnParamChanged(RCM::StrategyStudio::StrategyParam& param);

    //
    // Strategy overrides
    //
   private:
    virtual void RegisterForStrategyEvents(
        RCM::StrategyStudio::StrategyEventRegister* eventRegister,
        RCM::StrategyStudio::DateType currDate);
    virtual void DefineStrategyParams();
    virtual void DefineStrategyCommands();

    //
    // Internal helpers
    //
    /** Compute the weighted imbalance I for a given instrument. */
    double ComputeWeightedImbalance(
        const RCM::StrategyStudio::MarketModels::Instrument& inst) const;

    /** Apply entry/exit rules based on the latest imbalance. */
    void EvaluateImbalanceSignal(
        const RCM::StrategyStudio::MarketModels::Instrument& inst,
        double imbalance, RCM::StrategyStudio::TimeType event_time);

    /** Convenience wrappers for entering / exiting a long position. */
    void EnterLong(const RCM::StrategyStudio::MarketModels::Instrument& inst);
    void ExitLong(const RCM::StrategyStudio::MarketModels::Instrument& inst);

    //
    // Strategy parameters (configurable from Strategy Manager)
    //
    int m_num_levels;          ///< n : number of price levels to look at
    double m_entry_threshold;  ///< t : entry threshold on imbalance
    double m_exit_threshold;   ///< exit threshold (often 0)
    int m_persistence_len;     ///< l : ticks imbalance must persist
    double m_weight_exponent;  ///< w : exponent on (i+1)^w
    double m_latency_ns;       ///< a : assumed latency in nanoseconds

    int m_position_size;  ///< order size when entering/exiting
    bool m_debug_on;      ///< enable/disable verbose logging

    //
    // Per-instrument state
    //
    PersistenceMap m_persistence_map;  ///< consecutive ticks with signal
    ImbalanceMap m_last_imbalance;     ///< last computed imbalance

    boost::unordered_map<const RCM::StrategyStudio::MarketModels::Instrument*,
                         RCM::StrategyStudio::OrderID>
        m_instrument_order_id_map;
};

//
// C-style exports expected by Strategy Studio loader
//
extern "C" {

_STRATEGY_EXPORTS const char* GetType() {
    return "WobiSignalStrategy";
}

_STRATEGY_EXPORTS RCM::StrategyStudio::IStrategy* CreateStrategy(
    const char* strategyType, unsigned strategyID, const char* strategyName,
    const char* groupName) {
    if (strcmp(strategyType, GetType()) == 0) {
        return *(new WobiSignalStrategy(strategyID, strategyName, groupName));
    }
    return NULL;
}

_STRATEGY_EXPORTS const char* GetAuthor() {
    return "dlariviere";
}

_STRATEGY_EXPORTS const char* GetAuthorGroup() {
    return "UIUC";
}

_STRATEGY_EXPORTS const char* GetReleaseVersion() {
    return RCM::StrategyStudio::Strategy::release_version();
}
}

#endif
