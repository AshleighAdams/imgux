#!/bin/bash

export LD_LIBRARY_PATH=.

rm frame.raw &> /dev/null; mkfifo frame.raw
rm flow.raw &> /dev/null;  mkfifo flow.raw
 
INPUT_SOURCE=0
INPUT_OPTIONS=--rotate=180
FLOW_SCALE=0.7

./flow-motiontrack --background-frame=frame.raw --flow-frame=flow.raw \
	|./showframe --title="Tracked" \
> /dev/null &

./videosource "$INPUT_SOURCE" $INPUT_OPTIONS \
	|./showframe --title="Frame" \
	|tee frame.raw \
	|./opticalflow --visualize --scalar=$FLOW_SCALE \
> flow.raw

#\
#	|./ftee frame.raw \
#	
#> flow.raw

wait
