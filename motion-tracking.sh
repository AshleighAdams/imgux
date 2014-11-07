#!/bin/bash

export LD_LIBRARY_PATH=.

rm bg.raw &> /dev/null;    mkfifo bg.raw
rm flow.raw &> /dev/null;  mkfifo flow.raw
 
INPUT_SOURCE=0
#INPUT_OPTIONS=--rotate=180
FLOW_SCALE=0.5 # decrease this until your CPU's core usage is does not exceede the core's maximium (else it won't run in real time)

# just draws a box for now...
./flow-motiontrack --background-frame=bg.raw --flow-frame=flow.raw \
	|./showframe --title="Tracked" \
	|./recordframes --file="tracked.avi" \
> /dev/null &

#./videosource "$INPUT_SOURCE" $INPUT_OPTIONS \
./screensource --scale=0.25 \
	|tee bg.raw \
	|./opticalflow --winsize=20 --visualize --scale=$FLOW_SCALE \
> flow.raw

wait
