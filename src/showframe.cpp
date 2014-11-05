#include <imgux.hpp>

#include <iostream>
#include <string>
#include <sstream>

#include <opencv2/highgui/highgui.hpp>

int main(int argc, char** argv)
{
	imgux::arguments_add("title", "frame", "Default title");
	imgux::arguments_add("write-frame", "1", "Write the frame back out?");
	imgux::arguments_parse(argc, argv);
	
	std::string	title;
	bool write_frame;
	imgux::arguments_get("title", title);
	imgux::arguments_get("write-frame", write_frame);
	
	imgux::frame_setup();
	cv::namedWindow(title, cv::WINDOW_AUTOSIZE);
	
	cv::Mat mat;
	imgux::frame_info info;
	
	while(true)
	{
		if(!imgux::frame_read(mat, info))
			break;
		
		cv::imshow(title, mat);
		cv::waitKey(1);
		
		if(write_frame)
		{
			imgux::frame_write(mat, info);
		}
	}
	
	return 0;
}
