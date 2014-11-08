#include <imgux.hpp>

#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <ctime>

double time()
{
	using namespace std;
	using namespace std::chrono;

	static high_resolution_clock::time_point start = high_resolution_clock::now();
	high_resolution_clock::time_point now = high_resolution_clock::now();

	duration<double> time_span = duration_cast<duration<double>>(now - start);
	
	return time_span.count();
}

void rotate_image_90n(cv::Mat &src, cv::Mat &dst, int angle)
{   
   if(src.data != dst.data){
       src.copyTo(dst);
   }

   angle = ((angle / 90) % 4) * 90;

   //0 : flip vertical; 1 flip horizontal
   bool const flip_horizontal_or_vertical = angle > 0 ? 1 : 0;
   int const number = std::abs(angle / 90);          

   for(int i = 0; i != number; ++i){
       cv::transpose(dst, dst);
       cv::flip(dst, dst, flip_horizontal_or_vertical);
   }
}

int main(int argc, char** argv)
{
	imgux::arguments_add("rotate", "0", "Apply some rotation (90,180,270)");
	imgux::arguments_add("scale", "1", "Scale the image");
	imgux::arguments_parse(argc, argv);
	std::vector<std::string> args = imgux::arguments_get_list();
	
	int rotate = 0;
	double scale = 0;
	imgux::arguments_get("rotate", rotate);
	imgux::arguments_get("scale", scale);
	
	if(args.size() < 2)
	{
		std::cerr << "usage: videosource <path|deviceid>\n";
		return 1;
	}
	
	int index = 0;
	std::string file = args[1];
	
	try
	{
		index = std::stoi(file);
		
	}
	catch(std::invalid_argument ex)
	{
		index = -1;
	}
	
	cv::VideoCapture stream = index >= 0 ? cv::VideoCapture(index) : cv::VideoCapture(file);
	cv::Mat frame, frame_out;
	
	if(!(stream.read(frame))) //get one frame form video
	{
		std::cerr << "can't open video source " << file << "\n";
		return 1;
	}
	
	std::cerr << "video source " << file << " opened\n";
	imgux::frame_setup();
	
	cv::Size targsize = cv::Size((double)frame.size().width * scale, (double)frame.size().height * scale);
	
	size_t i = 0;
	
	while(true)
	{
		double t = time();
		std::stringstream ss;
		ss << std::fixed << "time=" << t << ";frame=" << i++ << ";source=" << file;
		
		imgux::frame_info info;
		info.info = ss.str();
		
		cv::resize(frame, frame_out, targsize);
		
		if(rotate != 0)
			rotate_image_90n(frame_out, frame_out, rotate);
		
		imgux::frame_write(frame_out, info);
		
		if(!(stream.read(frame)))
		{
			std::cerr << "stream finished\n";
			break;
		}
	}
	
	return 0;
}
