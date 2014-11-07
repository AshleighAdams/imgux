#ifndef imgux_HPP
#define imgux_HPP

// STL
#include <iostream>
#include <cassert>

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
	void frame_close();
	std::istream* frame_default_input();
	std::ostream* frame_default_output();
	
	// generic things to read frame infos
	double frameinfo_time(const imgux::frame_info& info);
	
	// opencv
	bool frame_read(cv::Mat& output, imgux::frame_info& info, std::istream& instream);
	bool frame_write(const cv::Mat& input, const imgux::frame_info& info, std::ostream& ostream);
	
	// Templated functions
	template<typename T>
	inline bool frame_read(T& output, frame_info& info)
	{
		assert(imgux::frame_default_input() != nullptr);
		return imgux::frame_read(output, info, *imgux::frame_default_input());
	}
	
	template<typename T>
	inline bool frame_write(const T& input, const frame_info& info)
	{
		assert(imgux::frame_default_output() != nullptr);
		return imgux::frame_write(input, info, *imgux::frame_default_output());
	}
	
}

#endif
