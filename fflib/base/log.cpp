#include "base/fftype.h"
#include "base/log.h"
#include "base/atomic_op.h"
#include "base/str_tool.h"
#include "base/arg_helper.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sys/time.h>
using namespace std;
using namespace ff;


StrFormat::StrFormat(const char* fmt_):
	m_fmt(fmt_),
	cur_format_index(0)
{
	m_fmt_len = ::strlen(fmt_);
}

StrFormat::~StrFormat()
{

}

bool StrFormat::moveToNextWildcard()
{
	m_fmt_type.clear();
	char tmp = '\0';

	for (; cur_format_index < m_fmt_len; ++ cur_format_index)
	{
		tmp = m_fmt[cur_format_index];
		if (tmp != '%')
		{
			m_result += tmp;
			continue;
		}

		char next = m_fmt[cur_format_index + 1];
		if (next == '%')
		{
			cur_format_index += 1;
			m_result += next;
			continue;
		}

		//! 支持多种格式化 %c %s, %d, %ld, %u, %lu, %x, %X, 找到格式化的类型
		//for (++cur_format_index; cur_format_index < m_fmt_len; ++ cur_format_index)
		for (unsigned int i = 1 ; i <= 5; ++i)
		{
			char cur = m_fmt[cur_format_index + i];

			if (cur == '\0' || cur == '%')
			{
				break;
			}
			else if (cur == 'c' || cur == 'd' || cur == 'u' || cur == 'x' ||
					 cur == 'f' || cur == 's' || cur == 'p')
			{
				m_fmt_type.type    = cur;
				m_fmt_type.min_len = ::atoi(m_fmt + cur_format_index + 1);
				cur_format_index   = cur_format_index + i + 1;
				if (next == '0')
				{
					m_fmt_type.fill_char = next;
				}
				return true;
			}
			else if (cur == 'l')
			{
				char c_num = m_fmt[cur_format_index + i + 1];
				if ('d' == c_num || 'u' == c_num)
				{
					m_fmt_type.type    = c_num;
					m_fmt_type.min_len = ::atoi(m_fmt + cur_format_index + 1);
					cur_format_index   = cur_format_index + i + 2;
					if (next == '0')
					{
						m_fmt_type.fill_char = next;
					}
					return true;
				}
			}
		}
		m_result += tmp;

	}
	return false;
}

void StrFormat::append(const char* str_)
{
	if (moveToNextWildcard())
	{
		unsigned int len = ::strlen(str_);
		int width = m_fmt_type.min_len > len? m_fmt_type.min_len -len: 0;
		for (; width > 0; -- width)
		{
			m_result += m_fmt_type.fill_char;
		}
	}

	m_result += str_;
}
void StrFormat::append(const string& str_)
{
	if (moveToNextWildcard())
	{
		int width = m_fmt_type.min_len > str_.length()? m_fmt_type.min_len -str_.length(): 0;
		for (; width > 0; -- width)
		{
			m_result += m_fmt_type.fill_char;
		}
	}

	m_result += str_;
}

const string& StrFormat::genResult()
{
	if (cur_format_index < m_fmt_len)
	{
		m_result.append(m_fmt + cur_format_index);
	}
	return m_result;
}


Log::Log(int level_, const string& all_class_, const string& path_, const string& file_,
		  	 bool print_file_, bool print_screen_):
	m_enabled_level(0),
	m_enable_class_set(new str_set_t()),
	m_file_name_index(0),
	m_line_num(0)
{
	::memset(&m_last_create_dir_tm, 0, sizeof(m_last_create_dir_tm));
	m_class_set_history.push_back(m_enable_class_set);

	for (int i = 0; i < level_ && i < LOG_LEVEL_NUM; ++i)
	{
		m_enabled_level |= (1 << i);
	}

	vector<string> vt_class;
	StrTool::split(all_class_, vt_class);
	for (size_t i = 0; i < vt_class.size(); ++i)
	{
		m_enable_class_set->insert(vt_class[i]);
	}

	m_path 	      	= path_;
	m_filename      = file_;
	m_enable_file   = print_file_;
	m_enable_screen = print_screen_;
}

Log::~Log()
{
	for (ptr_vt_t::iterator it = m_class_set_history.begin(); it != m_class_set_history.end(); ++it)
	{
		delete (*it);
	}
	m_class_set_history.clear();
}

void Log::setLevel(int level_, bool flag_)
{
	if (flag_)
	{
		m_enabled_level |= (1 << level_);
	}
	else
	{
		m_enabled_level &= ~(1 << level_);
	}
}

void Log::setModule(const string& class_, bool flag_)
{
	str_set_t* pset = new str_set_t(m_enable_class_set->begin(), m_enable_class_set->end());
	if (flag_)
	{
		pset->insert(class_);
	}
	else
	{
		pset->erase(class_);
	}

	m_class_set_history.push_back(pset);
	ATOMIC_SET(&m_enable_class_set, pset);
}

bool Log::is_level_enabled(int level_)
{
	return m_enabled_level & (1 << level_);
}

void Log::setPrintFile(bool flag_)
{
	m_enable_file = flag_;
}
void Log::setPrintScreen(bool flag_)
{
	m_enable_screen = flag_;
}

const char* Log::find_class_name(const char* class_)
{
	str_set_t* pset = ATOMIC_FETCH(&m_enable_class_set);
	str_set_t::iterator it = pset->find(class_);
	if (it != pset->end())
	{
		return (*it).c_str();
	}
	return NULL;
}

static const char* g_log_level_desp[] =
{
	"FATAL",
	"ERROR",
	"WARN ",
	"INFO ",
	"TRACE",
	"DEBUG"
};
#ifdef _WIN32
static const char* g_log_color_head[] =
{
	"",
	"",
	"",
	"",
	"",
	""
};
static const char* g_log_color_tail[] =
{
	"",
	"",
	"",
	"",
	"",
	""
};
#else
static const char* g_log_color_head[] =
{
	"\033[0;35m",
	"\033[0;31m",
	"\033[1;34m",
	"\033[1;32m",
	"",
	"\033[1;33m"
};
static const char* g_log_color_tail[] =
{
	"\033[0m",
	"\033[0m",
	"\033[0m",
	"\033[0m",
	"",
	"\033[0m"
};
#endif
void Log::log_content(int level_, const char* str_class_, const string& content_, long tid_)
{
	struct timeval curtm;
	gettimeofday(&curtm, NULL);
	struct tm tm_val = *localtime(&(curtm.tv_sec));

	char log_buff[512];
	::snprintf(log_buff, sizeof(log_buff), "%02d:%02d:%02d.%03ld %s [%ld] [%s] ",
			tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, curtm.tv_usec/1000,
			g_log_level_desp[level_], tid_, str_class_);

	if (m_enable_file && check_and_create_dir(&tm_val))
	{
		m_file << log_buff << content_ << endl;
		m_file.flush();
	}

	if (m_enable_screen)
	{
		printf("%s%s%s%s\n", g_log_color_head[level_], log_buff, content_.c_str(), g_log_color_tail[level_]);
	}
}

bool Log::check_and_create_dir(struct tm* tm_val_)
{
	bool time_equal = (m_last_create_dir_tm.tm_mday == tm_val_->tm_mday &&
					  m_last_create_dir_tm.tm_mon == tm_val_->tm_mon &&
					  m_last_create_dir_tm.tm_year == tm_val_->tm_year);

	char file[1024];

	if (false == time_equal)
	{
		int rc = ::access(m_path.c_str(), F_OK);
		if (0 != rc)
		{
			rc = MKDIR(m_path.c_str());
			if (rc != 0)
			{
				printf("log mkdir<%s>failed error<%s>\n", m_path.c_str(), ::strerror(errno));
				return false;
			}
		}

		::snprintf(file, sizeof(file), "%s/%d-%02d-%02d",
				   m_path.c_str(), tm_val_->tm_year + 1900, tm_val_->tm_mon + 1, tm_val_->tm_mday);
		rc = ::access(file, F_OK);
		if (0 != rc)
		{
			rc = MKDIR(file);
			if (rc != 0)
			{
				printf("log mkdir file<%s>failed error<%s>\n", file, ::strerror(errno));
				return false;
			}
		}
		m_file_name_index = 0;
	}

	if (false == time_equal || m_line_num >= MAX_LINE_NUM)
	{
		for (int i = 0; i < 10000; ++i)
		{
			::snprintf(file, sizeof(file), "%s/%d-%02d-%02d/%s.%d",
					   m_path.c_str(), tm_val_->tm_year + 1900, tm_val_->tm_mon + 1, tm_val_->tm_mday,
					   m_filename.c_str(), ++m_file_name_index);

			int rc = ::access(file, F_OK);
			if (0 == rc) {
				continue;
			}
			break;
		}
		m_file.close();
		m_file.open(file);
		m_line_num = 0;
		m_last_create_dir_tm = *tm_val_;
	}
	return true;
}

LogService::LogService():
	m_log(NULL)
{

}
LogService::~LogService()
{
	stop();
}

int LogService::start(const string& opt_)
{
	ArgHelper arg(opt_);
    return start(arg);   
} 
int LogService::start(ArgHelper& arg)
{   
	if (m_log) return 0;

	int level = 2;
	string path = "./log";
	string filename = "log";
	bool print_file = true;
	bool print_screen = false;

	if (!arg.getOptionValue("-log_level").empty()) level = ::atoi(arg.getOptionValue("-log_level").c_str());
	if (!arg.getOptionValue("-log_path").empty()) path = arg.getOptionValue("-log_path");
	if (!arg.getOptionValue("-log_filename").empty()) filename = arg.getOptionValue("-log_filename");

	if (arg.getOptionValue("-log_print_file") == "false" || arg.getOptionValue("-log_print_file") == "0")
	{
		print_file = false;
	}
	if (arg.getOptionValue("-log_print_screen") == "true" || arg.getOptionValue("-log_print_screen") == "1")
	{
		print_screen = true;
	}

	m_log = new Log(level, arg.getOptionValue("-log_class"), path, filename, print_file, print_screen);
	m_thread.create_thread(TaskBinder::gen(&TaskQueue::run, &m_task_queue), 1);

	return 0;
}

int LogService::stop()
{
	if (NULL == m_log) return 0;

	m_task_queue.close();
	m_thread.join();
	delete m_log;
	m_log = NULL;
	return 0;
}

void LogService::setLevel(int level_, bool flag_)
{
	m_task_queue.post(TaskBinder::gen(&Log::setLevel, m_log, level_, flag_));
}

void LogService::setModule(const string& class_, bool flag_)
{
	m_task_queue.post(TaskBinder::gen(&Log::setModule, m_log, class_, flag_));
}
void LogService::setPrintFile(bool flag_)
{
	m_task_queue.post(TaskBinder::gen(&Log::setPrintFile, m_log, flag_));
}
void LogService::setPrintScreen(bool flag_)
{
	m_task_queue.post(TaskBinder::gen(&Log::setPrintScreen, m_log, flag_));
}
