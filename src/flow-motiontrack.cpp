#include <imgux.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <functional>
#include <mutex>

struct Rectf
{
	float x, y, w, h;
	Rectf(float x, float y, float w, float h) : x(x), y(y), w(w), h(h)
	{
	}
};

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
	
	cv::Scalar red(0, 0, 255);
	
	std::vector<Rectf> targets;
	std::mutex targets_lock;
	
	std::thread t_bg([&]
	{
		while(running)
		{
			if(!imgux::frame_read(bg, bginfo, bgstream))
				break;
			
			targets_lock.lock();
			
			for(const Rectf& rect : targets)
			{
				cv::Rect cvrect(rect.x*bg.cols, rect.y*bg.rows, rect.w*bg.cols, rect.h*bg.rows);
				cv::rectangle(bg, cvrect, red);
			}
			
			targets_lock.unlock();
			imgux::frame_write(bg, bginfo);
		}
		running = false;
	});
	
	cv::Mat blobs;
	bool first = true;
	
	enum checkfor
	{
		nomotion,
		motion,
		scanned
	};
	
	std::function<void(int,int,std::function<bool(int,int,checkfor)>)> scanline; scanline = [&blobs,&scanline](int x, int y, std::function<bool(int,int,checkfor)> func)
	{
		int width = blobs.cols, height = blobs.rows;
		
		if(x >= width or y >= height or x < 0 or y < 0)
			return;
		
		if( func(x, y, checkfor::nomotion) )
			return;
		
		int y1, x1;
		
		//draw current scanline from start position to the top
		y1 = y;
		while(y1 < height and func(x, y1, checkfor::motion))
			y1++;
		
		//draw current scanline from start position to the bottom
		y1 = y - 1;
		while(y1 >= 0 and func(x, y, checkfor::motion))
			y1--;
		
		//test for new scanlines to the left and right then create seeds
		y1 = y;
		while(y1 < height and func(x, y1, checkfor::scanned))
		{
			if(x > 0 and func(x - 1, y1, checkfor::motion))
				scanline(x - 1, y1, func);
			if(x < (width - 1) and func(x + 1, y1, checkfor::motion))
				scanline(x + 1, y1, func);
			y1++;
		}
		y1 = y - 1;
		while(y1 >= 0 and func(x, y1, checkfor::scanned))
		{
			if(x > 0 and func(x - 1, y1, checkfor::motion))
				scanline(x - 1, y1, func);
			if(x < (width - 1) and func(x + 1, y1, checkfor::motion))
				scanline(x + 1, y1, func);
			y1--;
		}
	};
	
	auto is_motion = [&](int x, int y, float threshold)
	{
		cv::Point2f vel = flow.at<cv::Point2f>(y, x);
		float speed = sqrt(vel.x*vel.x + vel.y*vel.y);
		return speed > threshold;
	};
	
	while(running)
	{
		if(!imgux::frame_read(flow, flowinfo, flowstream))
			break;
		
		if(first)
		{
			first = false;
			blobs.create(flow.size(), CV_8UC1);
		}
		blobs = cv::Scalar(0); // reset it
		
		// blur it
		//cv::blur(flow, flow, cv::Size(5, 5));
		
		float big_threshold = 1.0;
		float small_threshold = big_threshold / 5;
		
		targets_lock.lock();
		targets.clear();
		
		for(int y = 0; y < flow.rows; y++)
		for(int x = 0; x < flow.cols; x++)
		{
			if(blobs.at<uchar>(y,x) == 0 and is_motion(x, y, big_threshold))
			{
				int minx = x, maxx = x, miny = y, maxy = y;
				scanline(x, y, [&blobs,&maxx,&maxy,&minx,&miny,small_threshold,&is_motion](int xx, int yy, checkfor c)
				{
					bool ret;
					uchar& b = blobs.at<uchar>(yy, xx);
					
					if(c == checkfor::nomotion)
					{
						ret = not is_motion(xx, yy, small_threshold) and b == 0;
					}
					else if(c == checkfor::motion)
					{
						ret = is_motion(xx, yy, small_threshold) and b == 0;
						
						if(ret) // update to scanned
						{
							if(xx < minx) minx = xx;
							if(xx > maxx) maxx = xx;
							if(yy < miny) miny = yy;
							if(yy > maxy) maxy = yy;
							
							b = 1;
						}
					}
					else if(c == checkfor::scanned)
						ret = b == 1;
					
					return ret;
				});
				
				// scanline complete, we now have a blob, update the target's vector
				float xperc = (float)minx / (float)blobs.cols;
				float yperc = (float)miny / (float)blobs.rows;
				float sizex = float(maxx - minx) / (float)blobs.cols;
				float sizey = float(maxy - miny) / (float)blobs.rows;
				
				if(sizex > 0.01 and sizey > 0.01)
					targets.emplace_back(xperc, yperc, sizex, sizey);
			}
		}
		
		targets_lock.unlock();
		
		// scanline
		//u.at<Point2f>(y, x);
		// get blobs
			// do it in a way where you hit a high threshold blob, then when scan-lining, allow lower threshold blobs to join
		// try to match blobs to an object
	}
	
	running = false;
	t_bg.join();
	
	return 0;
}
