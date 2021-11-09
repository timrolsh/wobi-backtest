# Prerequisites
#
# Copy this script to /home/vagrant/Desktop/strategy_studio/backtesting/utilities/ with the command line executable
# Copy cmd_config_example.txt to cmd_config.txt by: cp cmd_config_example.txt cmd_config.txt
# Change the username in the above file to dlariviere
#
# Look at the below variables and modify them


instanceName="S6TEAM"
strategyName="DiaIndexArbStrategy"
startDate="2021-11-05"
endDate="2021-11-05"
sharedObjectFileName="DiaIndexArb.so"

strategyFolderPath="/home/vagrant/Desktop/strategy_studio/localdev/RCM/StrategyStudio/examples/strategies/DiaIndexArb/"
sharedObjectOriginPath="$strategyFolderPath/$sharedObjectFileName"
sharedObjectDestinationPath="/home/vagrant/Desktop/strategy_studio/backtesting/strategies_dlls"


# Remake the .so files after making modifications to the source code
(cd $strategyFolderPath && make clean && make all)
# Copy .so file from Strategy folder to the destination folder
cp $sharedObjectOriginPath $sharedObjectDestinationPath

# Run BackTesting Server in the background
(cd /home/vagrant/Desktop/strategy_studio/backtesting && ./StrategyServerBacktesting&)

/home/vagrant/Desktop/strategy_studio/backtesting/utilities/./StrategyCommandLine cmd strategy_instance_list
# Create the Instance of the given strategy, might throw errors if it exists, but thats okay
# /home/vagrant/Desktop/strategy_studio/backtesting/utilities/./StrategyCommandLine cmd create_instance $instanceName $strategyName UIUC SIM-1001-101 dlariviere 1000000 -symbols SPY

# Get the current number of lines in the main_log.txt file
logFileNumLines=$(wc -l < /home/vagrant/Desktop/strategy_studio/backtesting/logs/main_log.txt)

# DEBUGGING OUTPUT
echo "Number of lines Currently in Log file:",$logFileNumLines

# Start the backtest
/home/vagrant/Desktop/strategy_studio/backtesting/utilities/./StrategyCommandLine cmd start_backtest $startDate $endDate $instanceName 1

# Get the line number which ends with finished. 
foundFinishedLogFile=$(grep -nr "finished.$" /home/vagrant/Desktop/strategy_studio/backtesting/logs/main_log.txt | gawk '{print $1}' FS=":"|tail -1)

# DEBUGGING OUTPUT
echo "Last line found:",$foundFinishedLogFile

# If the line ending with finished. is less than the previous length of the log file, then strategyBacktesting has not finished, 
# once its greater than the previous, it means it has finished.
while ((logFileNumLines > foundFinishedLogFile))
do
    foundFinishedLogFile=$(grep -nr "finished.$" /home/vagrant/Desktop/strategy_studio/backtesting/logs/main_log.txt | gawk '{print $1}' FS=":"|tail -1)

    #DEBUGGING OUTPUT
    # echo "HERE"
done

# DEBUGGING OUTPUT
# echo "Last line found after ending:",$foundFinishedLogFile

# Get the latest file in terms of modified time
latestCRA=$(ls -t /home/vagrant/Desktop/strategy_studio/backtesting/backtesting-results | head -1)

#DEBUGGING OUTPUT
# echo "latest CRA File is: $latestCRA"

# Run the export command to generate csv files
/home/vagrant/Desktop/strategy_studio/backtesting/utilities/./StrategyCommandLine cmd export_cra_file backtesting-results/$latestCRA backtesting-cra-exports

# Quit strategy studio working in the background
/home/vagrant/Desktop/strategy_studio/backtesting/utilities/./StrategyCommandLine cmd quit

# print PnL
cd /home/vagrant/Desktop/strategy_studio/backtesting/backtesting-cra-exports
latestPnL=$(ls -t grep *pnl* | head -1)
tail -1 $latestPnL
cd /home/vagrant/Desktop/strategy_studio/backtesting/utilities

# Generate portfolio metric
# python /home/vagrant/Desktop/strategy_studio/localdev/RCM/StrategyStudio/examples/fin566_fall_2020_group_three/metrics/calc_metric.py 

