#include <imgux.hpp>

#include <iostream>
#include <string>
#include <sstream>
#include <regex>

#include <opencv2/highgui/highgui.hpp>

int main(int argc, char** argv)
{
	imgux::arguments_add("file", "recording.avi", "The file to output to");
	imgux::arguments_parse(argc, argv);
	
	std::string	file;
	imgux::arguments_get("file", file);
	
	imgux::frame_setup();	
	cv::Mat mat;
	imgux::frame_info info;
	
	// measure the FPS
	double deltas = 0;
	double deltas_count = 0;
	
	double t = -1;
	for(int i = 0; i <= 10; i++)
	{
		if(!imgux::frame_read(mat, info))
			return 1;
		
		double tnow = imgux::frameinfo_time(info);
		
		if(t > 0)
		{
			deltas += tnow - t;
			deltas_count++;
		}
		
		t = tnow;
	}
	
	deltas /= deltas_count;
	int fps = std::round(1.0 / deltas);
	
	std::cerr << "recordframes: fps = " << fps << " (" << file << ")\n";
	
	cv::VideoWriter ov(file, CV_FOURCC('M','J','P','G'), fps, mat.size(), true);
	
	while(true)
	{
		if(!imgux::frame_read(mat, info))
			break;
		imgux::frame_write(mat, info);
		ov.write(mat);
	}
	
	return 0;
}
