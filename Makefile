# Conditional settings based on passed in variables
ifdef INTEL
    CC=icc
else
    CC=g++
endif

ifdef DEBUG
    CFLAGS=-c -g -fPIC -fpermissive -std=c++11
else
    CFLAGS=-c -fPIC -fpermissive -O3 -std=c++11
endif

LIBPATH=../../../libs/x64
INCLUDEPATH=../../../includes

INCLUDES=-I/usr/include -I$(INCLUDEPATH)
LDFLAGS=$(LIBPATH)/libstrategystudio_analytics.a $(LIBPATH)/libstrategystudio.a $(LIBPATH)/libstrategystudio_transport.a $(LIBPATH)/libstrategystudio_marketmodels.a $(LIBPATH)/libstrategystudio_utilities.a $(LIBPATH)/libstrategystudio_flashprotocol.a
LIBRARY=DiaIndexArb.so

SOURCES=DiaIndexArb.cpp
HEADERS=DiaIndexArb.h
 
OBJECTS=$(SOURCES:.cpp=.o)

all: $(HEADERS) $(LIBRARY)

$(LIBRARY) : $(OBJECTS)
	$(CC) -shared -Wl,-soname,$(LIBRARY).1 -o $(LIBRARY) $(OBJECTS) $(LDFLAGS)
	
.cpp.o: $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -rf *.o $(LIBRARY)

copy_strategy: all
	cp $(LIBRARY) /vagrant/ss/bt/strategies_dlls/.

launch_backtest: 
	#cd /home/vagrant/Desktop/strategy_studio/backtesting/utilities ; ./StrategyCommandLine cmd start_backtest 2021-11-05 2021-11-05 TestTwoDiaIndexArbStrategy 1
	cd /vagrant/ss/bt/utilities ; ./StrategyCommandLine cmd start_backtest 2021-11-05 2021-11-05 TestOneDiaIndexArbStrategy 1

run_backtest: all
	cd ~/Downloads/ss_backtesting ; echo $PWD ; ./run_backtest.sh

output_results: 
	export CRA_RESULT=`cd ~/Downloads/ss_backtesting ; find ./backtesting-results -name 'BACK*cra' | tail -n 1` ; \
	echo $$CRA_RESULT
	export TEST_VAR="hi" ; \
	echo $$TEST_VAR
	
