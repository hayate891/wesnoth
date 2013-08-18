/*
	Copyright (C) 2013 by Pierre Talbot <ptalbot@mopong.net>
	Part of the Battle for Wesnoth Project http://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "umcd/logger/logger.hpp"
#include "umcd/logger/detail/log_line.hpp"
#include "umcd/logger/detail/log_stream.hpp"
#include "umcd/boost/thread/lock_guard.hpp"

#include <boost/make_shared.hpp>

namespace umcd{

const char* logger::severity_level_name[] = {
	"trace",
	"debug",
	"info",
	"warning",
	"error",
	"fatal"
};

std::map<std::string, severity::level> logger::severity_str2enum;

void logger::default_logging_output()
{
	int sev;
	for(sev=0; sev <= severity::warning; ++sev)
	{
		set_output(static_cast<severity::level>(sev), boost::make_shared<detail::standard_log_stream>(std::cout));
	}
	for(; sev < severity::num_severity_level; ++sev)
	{
		set_output(static_cast<severity::level>(sev), boost::make_shared<detail::standard_log_stream>(std::cerr));
	}
}

// Returns the old cache.
logger::cache_ptr logger::make_new_cache()
{
	cache_ptr old_cache = cache_;
	lock_guard<boost::mutex> guard(cache_access_);
	cache_ = boost::make_shared<cache_type>();
	return old_cache;
}

std::string logger::make_header(severity::level sev) const
{
	return std::string("[") + severity_level_name[sev] + "] ";
}

void logger::set_log_output(const logging_info::severity_list& sev_list, const boost::shared_ptr<detail::log_stream>& stream)
{
	for(std::size_t i = 0; i < sev_list.size(); ++i)
	{
		set_output(sev_list[i], stream);
	}
}

void logger::set_standard_output(const logging_info::severity_list& sev_list, const std::ostream& stream)
{
	set_log_output(sev_list, boost::make_shared<detail::standard_log_stream>(stream));
}

void logger::set_files_output(const logging_info::file_list& files)
{
	for(std::size_t i=0; i < files.size(); ++i)
	{
		set_log_output(files[i].second, boost::make_shared<detail::file_log_stream>(files[i].first));
	}
}

void logger::init_severity_str2enum()
{
	for(int sev=0; sev < severity::num_severity_level; ++sev)
		severity_str2enum[severity_level_name[sev]] = static_cast<severity::level>(sev);
}

logger::logger()
: current_sev_lvl_(severity::trace)
, cache_(boost::make_shared<cache_type>())
{
	default_logging_output();
}

void logger::add_line(const umcd::detail::log_line_cache& line)
{
	lock_guard<boost::mutex> guard(cache_access_);
	cache_->push_back(umcd::detail::log_line(line));
}

void logger::run_once()
{
	cache_ptr old_cache = make_new_cache();
	boost::array<boost::shared_ptr<std::ostream>, severity::num_severity_level> log_streams;
	for(std::size_t i=0; i < severity::num_severity_level; ++i)
	{
		log_streams[i] = logging_output_[i]->stream();
	}

	for(std::size_t i=0; i < old_cache->size(); ++i)
	{
		const umcd::detail::log_line& line = (*old_cache)[i];
		*log_streams[line.severity_level] << make_header(line.severity_level) 
			<< boost::posix_time::to_simple_string(line.time) << ": "
			<< line.data
			<< "\n";
	}
}

void logger::load(const logging_info& log_info)
{
	set_severity(log_info.lower_limit());

	set_standard_output(log_info.to_cout(), std::cout);
	set_standard_output(log_info.to_cerr(), std::cerr);
	set_files_output(log_info.to_files());
}

void logger::set_severity(severity::level level)
{
	current_sev_lvl_ = level;
}

severity::level logger::get_current_severity() const
{
	return current_sev_lvl_;
}

void logger::set_output(severity::level sev, const boost::shared_ptr<detail::log_stream>& stream)
{
	logging_output_[sev] = stream;
}

detail::log_line_cache logger::get_logger(severity::level level)
{
	return detail::log_line_cache(*this, level);
}

} // namespace umcd
