#ifndef imgux_HPP
#define imgux_HPP

// STL
#include <iostream>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>

namespace imgux
{
	// argument stuffs
	
	// string
	void arguments_add(const std::string& name, const std::string& default_value, const std::string& hint);
	bool arguments_get(const std::string& name, std::string& output);
	bool arguments_get(const std::string& name, bool& output);
	bool arguments_get(const std::string& name, int& output);
	bool arguments_get(const std::string& name, double& output);
	std::vector<std::string> arguments_get_list();
		
	void arguments_parse(int argc, char** argv);
	
	
	// frame stuffs
	struct frame_info // infomration that may not be part of the image, but usefull
	{
		std::string info;
	};
	
	void frame_setup();
	
	std::string get_input_format();
	std::string get_output_format();
	
	// opencv
	bool frame_read(cv::Mat& output, frame_info& info, std::istream& instream);
	bool frame_read(cv::Mat& output, frame_info& info);
	bool frame_write(const cv::Mat& input, const frame_info& info);
	bool frame_write(const cv::Mat& input, const frame_info& info, std::ostream& ostream);
}

#endif
