# Weighted Order Book Imbalance Signal Strategy

Project proposal/description Notion:
[https://www.notion.so/Fin556-Project-Proposal-2a907f56d649808e8335cfbb375957ad?source=copy_link](https://www.notion.so/Fin556-Project-Proposal-2a907f56d649808e8335cfbb375957ad?source=copy_link)

## Setup Stuff

* To get cursor/vscode autocomplete to work, uninstall the Microsoft C/C++ extension and install the C/C++ extension from Anysphere. It uses the .clangd file to set up the dependencies
* Set up backtester config in the strategy backtester server
* Set up preferred feeds in the strategy backtester server
* Start the backtesting server using this command from the backtester root directory:
```bash
./StrategyServerBacktesting
```

* Set up the cmd_config in the utilities for the strategy command line client that talks to the server
* Use the client to register the strategy with the server, command:
```bash
./StrategyCommandLine cmd create_instance Anything MatchExportName UIUC SIM-1001-101 dlariviere 1000000 -symbols SPY
# Use ./StrategyCommandLine cmd create_instance help
# For a more detailed explanation of the command
```
* Verify that the strategy is registered by running:
```bash
./StrategyCommandLine cmd list_instances
```
* To find potential issues with the backtesting server loading the strategy, watch for its output when you run:
```bash
./StrategyCommandLine cmd recheck_strategies
```

Run the strategy:
```bash
./StrategyCommandLine cmd start_backtest 2024-04-10 2024-04-18 Anything 0
# Make sure to use 0 for L2 feed, 1 is just price updates, run help on this for more info
```
Export the results to csvs:
```bash
./StrategyCommandLine cmd export_cra_file ../backtesting-results/file.cra ~/output_dir/ true true
```
* The trade reports csvs is what we use for our report