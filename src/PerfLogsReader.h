#pragma once

#include <iostream>

#include <vector>
#include <string>
#include <Pdh.h>
#include <PdhMsg.h>
#include <unordered_map>
#include <optional>

#include "boost/json.hpp"

#pragma comment(lib,"pdh.lib")

class PerfCountersComp;
class PerfCountersObject;
class PerfCountersItem;

class PerfCounters {
public:
	PerfCounters(PDH_HLOG phDataSource);
	void read();
	const std::vector<PerfCountersComp>& getComputers() const { return computers_; }
private:
	const PDH_HLOG phDataSource_;
	std::vector<PerfCountersComp> computers_;
};

class PerfCountersComp {
public:
	PerfCountersComp(PDH_HLOG phDataSource, std::wstring computer);
	void read();
	const std::vector<PerfCountersObject>& getObjects() const { return objects_; }
	const std::wstring& getCompName() const { return computer_; }
private:
	const PDH_HLOG phDataSource_;
	std::wstring computer_;
	std::vector< PerfCountersObject> objects_;
};

class PerfCountersObject {
public:
	PerfCountersObject(PDH_HLOG phDataSource, std::wstring computer, std::wstring object);
	void read();
	const std::vector<std::wstring>& getInstances() const { return instances_; }
	const std::vector<std::wstring>& getCounters() const { return counters_; }
	const std::wstring& getObjName() const { return object_; }
private:
	const PDH_HLOG phDataSource_;
	std::wstring computer_;
	std::wstring object_;
	std::vector<std::wstring> counters_;
	std::vector<std::wstring> instances_;
};

struct Counter {
	Counter(
		const wchar_t* computer, const wchar_t* object, const wchar_t* instances, const wchar_t* counter,
		const wchar_t* computer_eng, const wchar_t* object_eng, const wchar_t* instances_eng, const wchar_t* counter_eng);
	std::wstring computer_;
	std::wstring object_;
	std::wstring instances_;
	std::wstring counter_;
	std::wstring national_name_;
	std::wstring computer_eng_;
	std::wstring object_eng_;
	std::wstring instances_eng_;
	std::wstring counter_eng_;
	std::wstring english_name_;

	HCOUNTER hCounter_;
	_PDH_RAW_COUNTER prevCounter_ = { PDH_CSTATUS_ITEM_NOT_VALIDATED , 0, 0, 0, 0 };
	std::optional<double> max_value_;
	std::optional<double> sum_value_;
	std::optional<std::size_t> count_value_;
};

struct Sample {
	uint64_t start_period_;
	uint64_t end_period_;
	SYSTEMTIME point_time_;
	std::vector<std::optional<double>> values_;
};

class PerfLogsReader {
public:
	PerfLogsReader();
	~PerfLogsReader();
	std::wstring executeCommandW(const std::string& cmd);
	std::string executeCommand(const std::string& cmd);
	bool open(const std::vector<std::wstring>& files);
	void close();
	bool read();
	const SYSTEMTIME& getStartTime() const { return start_time_; }
	const SYSTEMTIME& getEndTime() const { return end_time_; }
	std::vector<Sample> getValues(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points);
private:
	std::uint64_t pointsInPeriod(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points);
	std::string executeCommandOpen(const boost::json::array* j_array);
	std::string executeCommandRead();
	std::string executeCommandGetValues(boost::json::object* j_object);
	bool fillEngCountersFromRegistry();
	bool fillNationalIndicesFromRegistry();
	bool fillCounters();
	const std::wstring& getEngName(const std::wstring& national_name);
	bool createQuery();
	void messageErrorPdh(DWORD dwErrorCode);
	boost::json::object countersToJsonObject();

	PDH_HLOG phDataSource_;
	HQUERY phQuery_;
	std::unique_ptr<PerfCounters> perfCounters_;
	SYSTEMTIME start_time_;
	SYSTEMTIME end_time_;
	std::unordered_map<std::uint32_t, std::wstring> eng_counters_map_;
	std::unordered_map<std::wstring, std::uint32_t> national_index_counters_map_;
	std::vector<Counter> counters_;
	std::wstring message_error_;
};