#include "imgux.hpp"

// STL
#include <unordered_map>
#include <sstream>
#include <fstream>
// STD
#include <cassert>

using namespace imgux;

// arguments

struct argument
{
	std::string name, hint, value;
	bool set;
};
std::unordered_map<std::string, argument> arguments;
std::vector<std::string> arguments_list;

argument* get_argument(const std::string& name)
{
	auto node = arguments.find(name);
	if(node == arguments.end())
		return nullptr;
	return &node->second;
}

void imgux::arguments_add(const std::string& name, const std::string& default_value, const std::string& hint)
{
	assert(get_argument(name) == nullptr);
	argument arg;
	arg.name = name;
	arg.hint = hint;
	arg.value = default_value;
	arg.set = false;
	arguments[name] = arg;
}

bool imgux::arguments_get(const std::string& name, std::string& output)
{
	argument* arg = get_argument(name);
	if(!arg)
	{
		std::cerr << "argument " << name << " not created!\n";
		exit(1);
	}
	
	output = arg->value;
	return arg->set;
}

bool imgux::arguments_get(const std::string& name, bool& output)
{
	std::string valstr;
	bool ret = arguments_get(name, valstr);
	
	std::stringstream ss;
	ss << valstr;
	ss >> output;
	
	return ret;
}
bool imgux::arguments_get(const std::string& name, int& output)
{
	std::string valstr;
	bool ret = arguments_get(name, valstr);
	
	std::stringstream ss;
	ss << valstr;
	ss >> output;
	
	return ret;
}
bool imgux::arguments_get(const std::string& name, double& output)
{
	std::string valstr;
	bool ret = arguments_get(name, valstr);
	
	std::stringstream ss;
	ss << valstr;
	ss >> output;
	
	return ret;
}

std::vector<std::string> imgux::arguments_get_list()
{
	return arguments_list;
}

void imgux::arguments_parse(int argc, char** argv)
{
	imgux::arguments_add("input", "/dev/stdin", "Input file");
	imgux::arguments_add("output", "/dev/stdout", "Output file");
	
	bool readargs = true;
	for(int n = 0; n < argc; n++)
	{
		std::string v = argv[n];
		size_t len = v.length();
		
		if(v[0] == '-' and v[1] == '-' and readargs)
		{
			if(len == 2) // is a --, stop reading arguments
			{
				readargs = false;
				break;
			}
			
			std::stringstream namess, valuess;			
			size_t k;
			for(k = 2; k < len and v[k] != '='; k++)
				namess << v[k];
			k++;
			if(k < len) // is a value assigned here?
				for(; k < len; k++)
					valuess << v[k];
			else
				valuess << true;
			
			std::string name = namess.str(), value = valuess.str();
			argument* arg = get_argument(name);
			
			if(!arg)
			{
				std::cerr << "error: argument " << name << " not recognized!\n";
				exit(1);
				return;
			}
			
			arg->value = value;
			arg->set = true;
		}
		else
			arguments_list.push_back(v);
	}
}

std::istream* stream_in = nullptr;
std::ostream* stream_out = nullptr;
bool setup = false;

struct state_info
{
	bool first = true;
	int width = 0;
	int height = 0;
	size_t elm_size = 0;
	size_t type = 0;
	size_t data_size = 0;
};

void imgux::frame_setup()
{
	std::string input, output;
	
	
	imgux::arguments_get("input", input);
	imgux::arguments_get("output", output);
	
	stream_in = new std::ifstream(input);
	stream_out = new std::ofstream(output);
	setup = true;
}

// OpenCV frame read/writers
bool imgux::frame_read(cv::Mat& output, frame_info& info, std::istream& sin)
{
	if(sin.eof())
		return false;
	
	static std::unordered_map<const cv::Mat*, state_info> states;
	state_info& state = states[&output];
	
	if(state.first)
	{
		state.first = false;
		
		char fmt_buff[128];
		sin.getline(fmt_buff, 128);
		std::string fmt = fmt_buff;
		
		assert(fmt == "opencv-mat");
		
		sin.read((char*)&state.width,    sizeof(state.width));
		sin.read((char*)&state.height,   sizeof(state.height));
		sin.read((char*)&state.elm_size, sizeof(state.elm_size));
		sin.read((char*)&state.type,     sizeof(state.type));
		
		state.data_size = state.width * state.height * state.elm_size;
	}
	
	if(output.cols != state.width or output.rows != state.height or output.elemSize() != state.elm_size or output.type() != state.type)
		output.create(state.width, state.height, state.type);
	
	static char buff[128];
	sin.getline(buff, 128);
	
	info.info = buff;
	sin.read((char*)output.ptr(), state.data_size);
	return !sin.eof();
}

bool imgux::frame_write(const cv::Mat& input, const frame_info& info, std::ostream& sout)
{	
	static std::unordered_map<const cv::Mat*, state_info> states;
	state_info& state = states[&input];
	
	if(state.first)
	{
		state.first = false;
		state.width = input.rows; // am i sure this is correct? seems to work
		state.height = input.cols;
		state.elm_size = input.elemSize();
		state.type = input.type();
		state.data_size = state.width * state.height * state.elm_size;
		
		sout << "opencv-mat\n";
		sout.write((char*)&state.width, sizeof(state.width));
		sout.write((char*)&state.height, sizeof(state.height));
		sout.write((char*)&state.elm_size, sizeof(state.elm_size));
		sout.write((char*)&state.type, sizeof(state.type));
	}
	
	sout << info.info << "\n";
	sout.write((const char*)input.ptr(), state.data_size);
	
	return !sout.badbit;
}

bool imgux::frame_read(cv::Mat& output, frame_info& info)
{
	assert(setup);
	return imgux::frame_read(output, info, *stream_in);
}
bool imgux::frame_write(const cv::Mat& input, const frame_info& info)
{
	assert(setup);
	return imgux::frame_write(input, info, *stream_out);
}
