#!/bin/bash

# so we can find the libs in the current dir, and attempt to use user installed OpenCV libs first
export LD_LIBRARY_PATH=".:/usr/local/lib" #:$LD_LIBRARY_PATH"
export PATH=$PATH:.

rm .bg.pipe &> /dev/null;        mkfifo .bg.pipe
rm .flow.pipe &> /dev/null;      mkfifo .flow.pipe
rm .flow-vis.pipe &> /dev/null;  mkfifo .flow-vis.pipe
 
INPUT_SOURCE=0
#INPUT_OPTIONS=--rotate=180
FLOW_SCALE=0.5 # decrease this until your CPU's core usage is does not exceed the core's maximum (else it won't run in real time)

# just draws a box for now...
flow-motiontrack --background-frame=.bg.pipe --flow-frame=.flow.pipe \
	| showframe --title="Tracked" \
	| recordframes --file="tracked.avi" \
> /dev/null &

# record the flow to flow.avi
cat .flow-vis.pipe \
	| showframe --title="Optical Flow" \
	| recordframes --file="flow.avi" \
> /dev/null &

#videosource "$INPUT_SOURCE" $INPUT_OPTIONS \
screensource --scale=0.5 \
	| tee .bg.pipe \
	| opticalflow --winsize=20 --visualize --visualize-out=.flow-vis.pipe --scale=$FLOW_SCALE \
> .flow.pipe

wait
