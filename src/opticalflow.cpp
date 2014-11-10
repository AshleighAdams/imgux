#include <imgux.hpp>

#include <iostream>
#include <string>
#include <sstream>
#include <regex>

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/gpu/gpu.hpp>

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

double pyr_scale; int levels; int winsize; int iterations; int poly_n; double poly_sigma;
bool colourize, visualize;
double s = 1.0;

int main_gpu()
{
	imgux::frame_info info;
	
	std::stringstream ss;
	std::string frameinfo_ext;
	
	ss << ";flow-winsize=" << std::fixed << winsize << ";";
	frameinfo_ext = ss.str();	
	
	cv::Mat GetImg, flow_x, flow_y, next, prvs;

	//gpu variable
	cv::gpu::GpuMat prvs_gpu, next_gpu, flow_x_gpu, flow_y_gpu;
	cv::gpu::GpuMat prvs_gpu_o, next_gpu_o;
	cv::gpu::GpuMat prvs_gpu_c, next_gpu_c;

	// read frame
	imgux::frame_read(GetImg, info);
	
	//gpu upload, resize, color convert
	prvs_gpu_o.upload(GetImg);
	cv::gpu::resize(prvs_gpu_o, prvs_gpu_c, cv::Size(GetImg.size().width/s, GetImg.size().height/s) );
	cv::gpu::cvtColor(prvs_gpu_c, prvs_gpu, CV_BGR2GRAY);
	
	bool use_farneback = false;
	
	cv::gpu::FarnebackOpticalFlow farneback_flow;
	farneback_flow.pyrScale = pyr_scale;
	farneback_flow.numLevels = levels;
	farneback_flow.winSize = winsize;
	farneback_flow.numIters = iterations;
	farneback_flow.polyN = poly_n;
	farneback_flow.polySigma = poly_sigma;
	
	cv::gpu::BroxOpticalFlow brox_flow(0.197, 50.0, 0.8, 10, 77, 10);
	
	if(!use_farneback)
		prvs_gpu.convertTo(next_gpu, CV_32F, 1.0 / 255.0);
	
	/*
	BroxOpticalFlow gpu
	calcOpticalFlowSF

	*/
	
	cv::Mat flow;
	flow.create(cv::Size(GetImg.size().width/s, GetImg.size().height/s), CV_32FC2);
	
	//unconditional loop
	while (true)
	{
		if(!imgux::frame_read(GetImg, info))
			break;
		
		next_gpu_o.upload(GetImg);
		
		cv::gpu::resize(next_gpu_o, next_gpu_c, cv::Size(GetImg.size().width/s, GetImg.size().height/s) );
		cv::gpu::cvtColor(next_gpu_c, next_gpu, CV_BGR2GRAY);
		
		if(use_farneback)
			farneback_flow(prvs_gpu, next_gpu, flow_x_gpu, flow_y_gpu);
		else
		{
			next_gpu.convertTo(next_gpu, CV_32F, 1.0 / 255.0);
			brox_flow(prvs_gpu, next_gpu, flow_x_gpu, flow_y_gpu);
		}
		
		// download the result
		flow_x_gpu.download( flow_x );
		flow_y_gpu.download( flow_y );
		
		next_gpu.download( next );
		prvs_gpu.download( prvs );
		prvs_gpu = next_gpu.clone();
		
		// gpu stuff is done
		
		// fix the velocity part, and merge into one
		for(int y = 0; y < flow.rows; y++)
		for(int x = 0; x < flow.cols; x++)
		{
			cv::Point2f& vec = flow.at<cv::Point2f>(y, x);
			float fx = flow_x.at<float>(y, x);
			float fy = flow_y.at<float>(y, x);
			
			vec.x = fx * 15.0; // is this srsly 'cause of the FPS?
			vec.y = fy * 15.0;
		}
		
		//
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
		
		
		/*
		drawOptFlowMap_gpu(flow_x, flow_y, cflow, 10 , CV_RGB(0, 255, 0));
		imshow("OpticalFlowFarneback", cflow);
		*/
		
		
	}
	
	return 0;
}

int main_cpu()
{
	imgux::frame_info info;
	
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
		//cv::calcOpticalFlowSF(prvs, next, flow, 3, 2, 4, 4.1, 25.5, 18, 55.0, 25.5, 0.35, 18, 55.0, 25.5, 10);
		
		
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
	imgux::arguments_add("use-gpu", "auto", "auto|always|never");
	
	imgux::arguments_parse(argc, argv);
	
	imgux::arguments_get("pyr-scale", pyr_scale);
	imgux::arguments_get("levels", levels);
	imgux::arguments_get("winsize", winsize);
	imgux::arguments_get("iterations", iterations);
	imgux::arguments_get("poly-n", poly_n);
	imgux::arguments_get("poly-sigma", poly_sigma);
	
	
	imgux::arguments_get("colourize", colourize);
	imgux::arguments_get("scale", s);
	imgux::arguments_get("visualize", visualize);
	s = 1.0/s;

	if(visualize)
		cv::namedWindow("Optical Flow");
	
	imgux::frame_setup();
	
	std::string gpu;
	imgux::arguments_get("use-gpu", gpu);
	
	if(gpu == "auto")
	{
		int count = cv::gpu::getCudaEnabledDeviceCount();
		
		if(count == 0)
		{
			std::cerr << "opticalflow: no GPU found, using CPU\n";
			return main_cpu();
		}
		else
		{
			std::cerr << "opticalflow: GPU found\n";
			return main_gpu();
		}
	}
	else if(gpu == "always")
	{
		std::cerr << "opticalflow: forcing GPU\n";
		return main_gpu();
	}
	else if(gpu == "never")
	{
		std::cerr << "opticalflow: forcing CPU\n";
		return main_cpu();
	}
	else
	{
		std::cerr << "opticalflow: error: --use-gpu must be either auto, always, or never\n";
		return 1;
	}
}

