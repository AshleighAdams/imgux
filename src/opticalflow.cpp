#include <imgux.hpp>

#include <iostream>
#include <string>
#include <sstream>
#include <regex>

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/highgui/highgui.hpp>

static void HSVtoRGB(double h, double s, double v, double& r, double& g, double& b)
{
	h /= 360.0;
	r = g = b = 0;
	double i = floor(h * 6);
	double f = h * 6 - i;
	double p = v * (1 - s);
	double q = v * (1 - f * s);
	double t = v * (1 - (1 - f) * s);

	switch((int)i % 6)
	{
		case 0: r = v, g = t, b = p; break;
		case 1: r = q, g = v, b = p; break;
		case 2: r = p, g = v, b = t; break;
		case 3: r = p, g = q, b = v; break;
		case 4: r = t, g = p, b = v; break;
		case 5: r = v, g = p, b = q; break;
	}
	
	r *= 255;
	g *= 255;
	b *= 255;
}

static void colorizeFlow(const cv::Mat &u, cv::Mat &dst)
{
	using namespace cv;
	double max_vel = 10;
	
	dst.create(u.size(), CV_8UC3);
	for (int y = 0; y < u.rows; ++y)
	{
		for (int x = 0; x < u.cols; ++x)
		{
			Point2f vel = u.at<Point2f>(y, x);
			
			double ang = atan2(vel.x, vel.y) / M_PI * 180.0;
			if(ang < 0)
				ang += 360.0;
			
			double sat = ::sqrt(vel.x*vel.x + vel.y*vel.y);
			
			sat = sat / max_vel;
			sat = sat > 1.0 ? 1.0 : sat;
			
			double r = 0,g = 0, b = 0;
			HSVtoRGB(ang, sat, sat, r, g, b);
			
			/*
			double speed = sqrt(vel.x*vel.x + vel.y*vel.y) * delta;
			double r = 0,g = 0, b = 0;
			
			if(speed > 10)
				r = 255;
			else if(speed > 5)
				b = 255;
			*/
			dst.at<uchar>(y,3*x) = b;
			dst.at<uchar>(y,3*x+1) = g;
			dst.at<uchar>(y,3*x+2) = r;
		}
	}
}

int main(int argc, char** argv)
{		
	imgux::arguments_add("pyr-scale", "0.5", "");
	imgux::arguments_add("levels", "3", "");
	imgux::arguments_add("winsize", "15", "");
	imgux::arguments_add("iterations", "3", "");
	imgux::arguments_add("poly-n", "5", "");
	imgux::arguments_add("poly-sigma", "1.2", "");
	
	imgux::arguments_add("colourize", "0", "Should we produce a colourized hue (ang) sat (speed), val(speed) output");
	imgux::arguments_add("scale", "1.0", "Multiply size by this");
	imgux::arguments_add("visualize", "0", "Visualize the flow?");
	imgux::arguments_add("velocity-fix", "1", "Should we multiply the velocity by the frame time?");
	
	imgux::arguments_parse(argc, argv);
	double pyr_scale; int levels; int winsize; int iterations; int poly_n; double poly_sigma;
	
	imgux::arguments_get("pyr-scale", pyr_scale);
	imgux::arguments_get("levels", levels);
	imgux::arguments_get("winsize", winsize);
	imgux::arguments_get("iterations", iterations);
	imgux::arguments_get("poly-n", poly_n);
	imgux::arguments_get("poly-sigma", poly_sigma);
	
	bool colourize, visualize;
	double s = 1.0;
	imgux::arguments_get("colourize", colourize);
	imgux::arguments_get("scale", s);
	imgux::arguments_get("visualize", visualize);
	s = 1.0/s;
	
	imgux::frame_setup();
	imgux::frame_info info;
	
	//cv::calcOpticalFlowFarneback(mat_b, mat_a, mat_flow
	
	if(visualize)
		cv::namedWindow("Optical Flow");
	
	std::stringstream ss;
	std::string frameinfo_ext;
	
	ss << ";flow-winsize=" << std::fixed << winsize << ";";
	frameinfo_ext = ss.str();	
		
	cv::Mat GetImg;
	cv::Mat prvs, next;
	
	imgux::frame_read(GetImg, info);
	cv::resize(GetImg, prvs, cv::Size(GetImg.size().width/s, GetImg.size().height/s));
	cv::cvtColor(prvs, prvs, CV_BGR2GRAY);
	
	while (true)
	{
		if(!imgux::frame_read(GetImg, info))
			break;
		cv::resize(GetImg, next, cv::Size(GetImg.size().width/s, GetImg.size().height/s) );
		cv::cvtColor(next, next, CV_BGR2GRAY);		
		
		info.info += frameinfo_ext;
		
		cv::Mat flow;
		cv::calcOpticalFlowFarneback(prvs, next, flow, pyr_scale, levels, winsize, iterations, poly_n, poly_sigma, 0);// | cv::OPTFLOW_FARNEBACK_GAUSSIAN);
		
		for(int y = 0; y < flow.rows; y++)
		for(int x = 0; x < flow.cols; x++)
		{
			cv::Point2f& vec = flow.at<cv::Point2f>(y, x);
			vec.x *= 15.0; // is this srsly 'cause of the FPS?
			vec.y *= 15.0;
		}
		
		if(colourize || visualize)
		{
			cv::Mat cflow;
			cv::cvtColor(prvs, cflow, CV_GRAY2BGR);
			colorizeFlow(flow, cflow);
			
			if(colourize)
				imgux::frame_write(cflow, info);
			if(visualize)
			{
				cv::imshow("Optical Flow", cflow);
				cv::waitKey(1);
			}
		}
		
		if(!colourize)
			imgux::frame_write(flow, info);
		
		prvs = next.clone();
	}
	
	return 0;
}

