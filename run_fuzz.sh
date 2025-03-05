#!/bin/sh
 
export AFL_SKIP_CPUFREQ=1

# taken from https://stackoverflow.com/questions/3685970/check-if-a-bash-array-contains-a-value
IFS="|"
targets=("lexer")

if (($# != 1))
then
  echo "usage: $0 [$targets]"
  exit 1
fi

if [[ ! "${IFS}${targets[*]}${IFS}" =~ "${IFS}$1${IFS}" ]]; then
  echo "usage: $0 [$targets]"
  exit 1
fi

afl-fuzz -i fuzzer_corpus -c build/${1}_harness_cmplog -o afl-out -- build/${1}_harness

unset IFS
unset ALF_SKIP_CPUFREQ
