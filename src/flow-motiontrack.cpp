#include <imgux.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <functional>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <list>

size_t CONFIRMED_LIFETIME = 10;
size_t MAX_MISSING_TIME = 30;
double PROBABILITY_SIZE_GROW = 0.05; // grow the search area by 32px on a 640x640 image in all directions per second missing

struct Island
{
	double x, y, w, h, xvel, yvel;
	bool eaten = false, found_owner = false;
	double avg_xvel, avg_yvel, eaten_count;
	Island(double x, double y, double w, double h, double xvel, double yvel) : x(x), y(y), w(w), h(h), xvel(xvel), yvel(yvel), avg_xvel(xvel), avg_yvel(yvel), eaten_count(1)
	{
	}
	double cx() const
	{
		return x + w / 2.0;
	}
	double cy() const
	{
		return y + h / 2.0;
	}
};

struct Tracked
{
	double x=0, y=0, w=0, h=0, vx=0, vy=0, avgw=0, avgh=0;
	std::list<std::tuple<double, double>> _center_history;
	std::list<std::tuple<double, double>> _size_history;
	
	size_t lifetime = 0;
	size_t missing_for = 0;
	size_t id = 0;
	double cx() const
	{
		return x + w / 2.0;
	}
	double cy() const
	{
		return y + h / 2.0;
	}
	
	double probably_is(const Island& island, double delta) // TODO: factor delta in to distance eq.
	{
		delta = delta * double(this->missing_for + 1);
		double size_grow = double(this->missing_for) * delta * PROBABILITY_SIZE_GROW; // the longer ago we seen this, the bigger the area may occupy
		
		double targx = this->cx() + this->vx * delta;
		double targy = this->cy() + this->vy * delta;
		
		double distance_thresh_x = this->avgw + size_grow; // + 32px @ 640px
		double distance_x = std::abs(targx - island.cx());
		double distance_prob_x = 1.0 - distance_x / distance_thresh_x;
		
		double distance_thresh_y = this->avgh + size_grow;
		double distance_y = std::abs(targy - island.cy());
		double distance_prob_y = 1.0 - distance_y / distance_thresh_y;
		
		return (distance_prob_x + distance_prob_y) / 2.0;
	}
	
	double is(std::vector<Island*> islands, double delta)
	{
		delta = delta * double(this->missing_for + 1);
		double scx = this->cx();
		double scy = this->cy();
		
		double sx, sy, ex, ey; // startx/y endx/y
		bool first = true;
		
		for(Island* island : islands)
		{
			if(first)
			{
				sx = ex = island->x;
				sy = ey = island->y;
				first = false;
			}
			
			double iex = island->x + island->w;
			double iey = island->y + island->h;
			
			if(island->x < sx) sx = island->x;
			if(island->y < sy) sy = island->y;
			
			if(iex > ex) ex = iex;
			if(iey > ey) ey = iey;
		}
		
		this->x = sx;
		this->y = sy;
		this->w = ex - sx;
		this->h = ey - sy;
		
		if(this->lifetime > 1)
		{
			// calculate avg vel
			this->_center_history.push_back(std::tuple<double,double>{(this->cx() - scx) / delta, (this->cy() - scy) / delta});
			if(this->_center_history.size() > 5)
				this->_center_history.pop_front();
			this->vx = this->vy = 0;
			for(const auto& tpl : this->_center_history)
			{
				this->vx += std::get<0>(tpl);
				this->vy += std::get<1>(tpl);
			}
			this->vx /= (double)this->_center_history.size();
			this->vy /= (double)this->_center_history.size();
		}
		
		//calculate avg size		
		this->_size_history.push_back(std::tuple<double,double>{w, h});
		if(this->_size_history.size() > 5)
			this->_size_history.pop_front();
		this->avgw = this->avgh = 0;
		for(const auto& tpl : this->_size_history)
		{
			avgw += std::get<0>(tpl);
			avgh += std::get<1>(tpl);
		}
		avgw /= (double)this->_size_history.size();
		avgh /= (double)this->_size_history.size();
	}
};

int main(int argc, char** argv)
{
	imgux::arguments_add("background-frame", "", "The input frame to draw over");
	imgux::arguments_add("flow-frame", "/dev/stdin", "The input frame to for optical flow");
	imgux::arguments_add("threshold-big", "10", "Flow velocity to seed a frame.  Independant of frame size");
	imgux::arguments_add("threshold-small", "5", "Once a seed has been found, how greedy should we be?.  Independant of frame size");
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
	std::vector<Tracked> tracked;
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
			
			/*
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
				
				cv::rectangle(bg, cvrect, red);
				cv::line(bg, center, to, red);
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
				cv::line(bg, center, to, orange);
			}
			*/
			
			auto dotted_line = [](cv::Mat& mat, const cv::Point& from, const cv::Point& to, const cv::Scalar& col, int length = 10, float filled = 0.7)
			{
				cv::LineIterator it(mat, from, to, 8);
				int fill = (int)((float)length * filled);
				for(int i = 0; i < it.count; i++,it++)
				{
					if ( i % length > fill )
					{
						(*it)[0] = col[0];
						(*it)[1] = col[1];
						(*it)[2] = col[2];
					}
				}
			};
			
			auto dotted_rectangle = [&dotted_line](cv::Mat& mat, const cv::Rect& rect, const cv::Scalar& col, int length = 10, float filled = 0.7)
			{
				cv::Point tl{rect.x, rect.y};
				cv::Point br{rect.x + rect.width, rect.y + rect.height};
				cv::Point tr{br.x, tl.y};
				cv::Point bl{tl.x, br.y};
				
				dotted_line(mat, tl, tr, col, length, filled);
				dotted_line(mat, tr, br, col, length, filled);
				dotted_line(mat, br, bl, col, length, filled);
				dotted_line(mat, bl, tl, col, length, filled);
			};
			
			for(const Tracked& tg : tracked)
			{
				double td = t * double(tg.missing_for + 1);
				
				double dx = tg.x + tg.vx * t;
				double dy = tg.y + tg.vy * t;
				
				int x = (dx/* + tg.vx * td*/) * bg.cols;
				int y = (dy/* + tg.vy * td*/) * bg.rows;
				int w = tg.avgw * bg.cols;
				int h = tg.avgh * bg.rows;
				int vx = tg.vx * 1.0 * bg.cols;
				int vy = tg.vy * 1.0 * bg.rows;
								
				int bits = 4;
				int bitshifts = 1 << bits;
				
				cv::Rect cvrect(x,y,w,h);
				cv::Point center = cv::Point((x + w / 2.0), (y + w / 2.0));
				cv::Point to = cv::Point((center.x + vx), (center.y + vy));
				
				if((tg.lifetime - tg.missing_for) >= CONFIRMED_LIFETIME)
				{
					auto col = tg.missing_for < 5 ? green : yellow;
					cv::rectangle(bg, cvrect, col, 1, 8);
					cv::line(bg, center, to, col, 1, 8);
				}
				else
				{
					dotted_rectangle(bg, cvrect, orange);
					dotted_line(bg, center, to, orange);
				}
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
	
	double lastt = 0, delta = 0, t = 0;
	
	while(running)
	{
		if(!imgux::frame_read(flow, flowinfo, flowstream))
			break;
		
		t = imgux::frameinfo_time(flowinfo);
		delta = t - lastt;
		lastt = t;
		
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
			/*
			targets.erase(std::remove_if(targets_grouped.begin(), targets_grouped.end(), [](const Island& a)
			{
				return a.eaten;
			}), targets_grouped.end());*/
		}
		
		for(Island& self : targets_grouped)
		{
			self.xvel = self.avg_xvel /= self.eaten_count;
			self.yvel = self.avg_yvel /= self.eaten_count;
		}
		
		// attempt to match to targets
		std::unordered_map<Tracked*, std::vector<Island*>> map;
		static int id = 0;
		
		for(Island& i : targets_grouped)
		{
			if(i.eaten)
				continue;
			
			double best_prob = 0;
			Tracked* best = nullptr;
			
			for(Tracked& t : tracked)
			{
				double prob = t.probably_is(i, delta);
				
				if(best == nullptr or prob > best_prob)
				{
					best_prob = prob;
					best = &t;
				}
			}
			
			if(best and best_prob > 0)
			{
				auto& list = map[best];
				list.push_back(&i);
			}
			else // make new
			{
				Tracked nt;
				nt.id = ++id;
				nt.vx = nt.vy = nt.x = nt.y = nt.w = nt.h = 0;
				tracked.push_back(nt);
				Tracked& val = tracked.back();
				
				auto& list = map[&val];
				list.push_back(&i);
				
				std::cerr << "tracking " << id << "\n";
			}
		}
		
		
		auto write_update = [](const Tracked& self){
			// format: update: id=ID pos=X,Y size=W,H vel=VX,VY age=LIFETIME seen=MISSING_FOR
			std::cerr << "update target:" 
				<< " id="   << self.id
				<< " pos="  << self.x << "," << self.y
				<< " size=" << self.w << "," << self.h
				<< " vel="  << self.vx << "," << self.vy
				<< " age="  << self.lifetime
				<< " seen=" << self.missing_for
			<<"\n";
		};
		
		std::cerr << "update: " << t << "\n";
		
		for(Tracked& t : tracked)
		{
			auto it = map.find(&t);
			t.lifetime++;
			
			if(it == map.end())
				t.missing_for++;
			else
			{
				t.missing_for = 0;
				t.is(it->second, delta);
			}
			
			write_update(t);
		}
		
		tracked.erase(std::remove_if(tracked.begin(), tracked.end(), [](const Tracked& t)
		{
			bool ret = t.missing_for > MAX_MISSING_TIME;
			
			if(t.missing_for > 0 and t.lifetime < CONFIRMED_LIFETIME) // was probably noise, ignore this, remove it now
				ret = true;
			
			if(ret)
				std::cerr << "lost " << t.id << "\n";
			
			return ret;
		}), tracked.end());
		
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
