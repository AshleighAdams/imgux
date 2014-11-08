#include <imgux.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <functional>
#include <algorithm>
#include <mutex>

struct Island
{
	double x, y, w, h, xvel, yvel;
	bool eaten = false;
	double avg_xvel, avg_yvel, eaten_count;
	Island(double x, double y, double w, double h, double xvel, double yvel) : x(x), y(y), w(w), h(h), xvel(xvel), yvel(yvel), avg_xvel(xvel), avg_yvel(yvel), eaten_count(1)
	{
	}
	double cx()
	{
		return x + w / 2.0;
	}
	double cy()
	{
		return y + h / 2.0;
	}
};

int main(int argc, char** argv)
{
	imgux::arguments_add("background-frame", "", "The input frame to draw over");
	imgux::arguments_add("flow-frame", "/dev/stdin", "The input frame to for optical flow");
	imgux::arguments_add("threshold-big", "5", "Flow velocity to seed a frame.  Independant of frame size");
	imgux::arguments_add("threshold-small", "2.5", "Once a seed has been found, how greedy should we be?.  Independant of frame size");
	imgux::arguments_parse(argc, argv);
	
	std::string	background_frame, flow_frame;
	double threshold_big, threshold_small;
	
	imgux::arguments_get("background-frame", background_frame);
	imgux::arguments_get("flow-frame", flow_frame);
	imgux::arguments_get("threshold-big", threshold_big);
	imgux::arguments_get("threshold-small", threshold_small);
	
	assert(background_frame != "");
	assert(flow_frame != "");
	
	std::ifstream bgstream(background_frame);
	std::ifstream flowstream(flow_frame);
	
	cv::Mat flow, bg;
	imgux::frame_info flowinfo, bginfo;
	
	imgux::frame_setup();
	
	bool running = true;
	
	cv::Scalar red(0, 0, 255);
	cv::Scalar green(0, 255, 0);
	cv::Scalar orange(0, 128, 255);
	cv::Scalar yellow(0, 255, 255);
	
	std::vector<Island> targets;
	std::vector<Island> targets_grouped;
	std::mutex targets_lock;
	double frame_motion = 0;
	double frame_bg = 0;
	double winsize = 0, winsize_xperc = 0, winsize_yperc = 0;
	
	std::thread t_bg([&]
	{
		while(running)
		{
			if(!imgux::frame_read(bg, bginfo, bgstream))
				break;
			frame_bg = imgux::frameinfo_time(bginfo);
			
			double t = frame_bg - frame_motion;
			
			targets_lock.lock();
				
			for(const Island& island : targets)
			{
				int x = (island.x + island.avg_xvel * t) * bg.cols;
				int y = (island.y + island.avg_yvel * t) * bg.rows;
				int w = island.w * bg.cols;
				int h = island.h * bg.rows;
				int vx = island.xvel * 1.0 * bg.cols;
				int vy = island.yvel * 1.0 * bg.rows;
				
				x++;y++;w-=2;h-=2;
				cv::Rect cvrect(x,y,w,h);
				cv::Point center = cv::Point(x + w / 2, y + w / 2);
				cv::Point to = cv::Point(center.x + vx, center.y + vy);
				/*
				cv::rectangle(bg, cvrect, green);
				cv::line(bg, center, to, red);*/
			}
			
			for(const Island& island : targets_grouped)
			{
				if(island.eaten)
					continue;
					
				int x = (island.x + island.avg_xvel * t) * bg.cols;
				int y = (island.y + island.avg_yvel * t) * bg.rows;
				int w = island.w * bg.cols;
				int h = island.h * bg.rows;
				int vx = island.avg_xvel * 1.0 * bg.cols;
				int vy = island.avg_yvel * 1.0 * bg.rows;
				
				cv::Rect cvrect(x,y,w,h);
				cv::Point center = cv::Point(x + w / 2, y + w / 2);
				cv::Point to = cv::Point(center.x + vx, center.y + vy);
				
				cv::rectangle(bg, cvrect, orange);
				cv::line(bg, center, to, yellow);
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
			
			winsize = imgux::frameinfo_number("flow-winsize", flowinfo);
			if(winsize == 0)
			{
				std::cerr << "flow-motiontrack: could not locate flow-winsize, defaulting to 15: " << flowinfo.info << "\n";
				winsize = 15.0;
			}
			winsize_xperc = winsize / (double)flow.size().width;
			winsize_yperc = winsize / (double)flow.size().height;
		}
		blobs = cv::Scalar(0); // reset it
		
		// blur it
		//cv::blur(flow, flow, cv::Size(5, 5));
		
		float big_threshold = threshold_big;
		float small_threshold = threshold_small;
		
		targets_lock.lock();
		targets.clear();
		
		frame_motion = imgux::frameinfo_time(flowinfo);
		
		int minx = 0, maxx = 0, miny = 0, maxy = 0;
		double countvel=0, velx=0, vely = 0;
		auto testfunc = [&blobs,&maxx,&maxy,&minx,&miny,&countvel,&velx,&vely,small_threshold,&is_motion,&flow,&big_threshold](int xx, int yy, checkfor c)
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
					
					{
						cv::Point2f& vel = flow.at<cv::Point2f>(yy, xx);
						float speed = sqrt(vel.x*vel.x + vel.y*vel.y);
						
						if(speed > big_threshold) // only count velocity from the larger thresholds so noise and stuff doesn't play any roles
						{
							countvel++;
							velx += vel.x;
							vely += vel.y;
						}
					}

					b = 1;
				}
			}
			else if(c == checkfor::scanned)
				ret = b == 1;
			
			return ret;
		};
		
		for(int y = 0; y < flow.rows; y++)
		for(int x = 0; x < flow.cols; x++)
		{
			if(blobs.at<uchar>(y,x) == 0 and is_motion(x, y, big_threshold))
			{
				minx = maxx = x;
				miny = maxy = y;
				countvel = velx = vely = 0;
				scanline(x, y, testfunc);
				
				// scanline complete, we now have a blob, update the target's vector
				float xperc = (float)minx / (float)blobs.cols;
				float yperc = (float)miny / (float)blobs.rows;
				float sizex = float(maxx - minx) / (float)blobs.cols;
				float sizey = float(maxy - miny) / (float)blobs.rows;
				velx = velx / countvel / (float)blobs.rows;
				vely = vely / countvel / (float)blobs.rows;
				
				xperc += winsize_xperc / 2.0;
				sizex -= winsize_xperc; // don't /2, as when we took xperc away, this shifted half
				yperc += winsize_yperc / 2.0;
				sizey -= winsize_yperc;
				
				if(sizex > 0.01 and sizey > 0.01)
					targets.emplace_back(xperc, yperc, sizex, sizey, velx, vely);
			}
		}
		
		targets_grouped = targets; // copy them
		
		size_t count = targets_grouped.size();
		double eat_distance_perc = 200.0 / 100.0;
		// every time we eat one, set i back to 0 (to re-test for newly ate, and bigger)
		
		bool changing = true;
		int its = 0;
		while(changing)
		{
			if(its++ > 100)
			{
				std::cerr << "flow-motiontrack: warning: ran over 100 iterations\n";
				break;
			}
			
			changing = false;
			std::sort(targets_grouped.begin(), targets_grouped.end(), [](const Island& a, const Island& b)
			{
				return (a.w + a.h) < (b.w + b.h);
			});
		
			for(int i = 0; i < count; i++)
			{
				Island& self = targets_grouped[i];
				if(self.eaten)
					continue;
			
				for(int k = i + 1; k < count; k++)
				{
					Island &other = targets_grouped[k];
					if(other.eaten)
						continue;
				
					double distx = std::abs(self.cx() - other.cx());
					double disty = std::abs(self.cy() - other.cy());
				
					if(distx < self.w * eat_distance_perc and disty < self.h * eat_distance_perc)
					{
						// om-nom it
						other.eaten = true;
						
						double self_endx = self.x + self.w;
						double self_endy = self.y + self.h;
					
						double other_endx = other.x + other.w;
						double other_endy = other.y + other.h;
					
						double x = std::min(self.x, other.x);
						double y = std::min(self.y, other.y);
					
						double w = std::max(self_endx, other_endx) - x;
						double h = std::max(self_endy, other_endy) - y;
					
						self.x = x;
						self.y = y;
						self.w = w;
						self.h = h;
					
						self.avg_xvel += other.avg_xvel;
						self.avg_yvel += other.avg_yvel;
						self.eaten_count += other.eaten_count;
						changing = true;
					}
				}
			}
			
			// remove all eaten targets
			std::remove_if(targets_grouped.begin(), targets_grouped.end(), [](const Island& a)
			{
				return a.eaten;
			});
		}
		
		for(Island& self : targets_grouped)
		{
			self.xvel = self.avg_xvel /= self.eaten_count;
			self.yvel = self.avg_yvel /= self.eaten_count;
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
