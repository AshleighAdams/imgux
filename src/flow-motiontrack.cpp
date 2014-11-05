#include <imgux.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>

int main(int argc, char** argv)
{
	imgux::arguments_add("background-frame", "", "The input frame to draw over");
	imgux::arguments_add("flow-frame", "/dev/stdin", "The input frame to for optical flow");
	imgux::arguments_parse(argc, argv);
	
	std::string	background_frame, flow_frame;
	imgux::arguments_get("background-frame", background_frame);
	imgux::arguments_get("flow-frame", flow_frame);
	
	assert(background_frame != "");
	assert(flow_frame != "");
	
	std::ifstream bgstream(background_frame);
	std::ifstream flowstream(flow_frame);
	
	cv::Mat flow, bg;
	imgux::frame_info flowinfo, bginfo;
	
	imgux::frame_setup();
	
	bool running = true;
	
	std::thread t_bg([&]
	{
		while(running)
		{
			if(!imgux::frame_read(bg, bginfo, bgstream))
				break;
			imgux::frame_write(bg, bginfo);
		}
		running = false;
	});
	
	
	while(running)
	{
		if(!imgux::frame_read(flow, flowinfo, flowstream))
			break;
		
		// get blobs
			// do it in a way where you hit a high threshold blob, then when scan-lining, allow lower threshold blobs to join
	}
	
	running = false;
	t_bg.join();
	
	return 0;
}
