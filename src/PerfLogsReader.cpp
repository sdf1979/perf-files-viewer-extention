#include "PerfLogsReader.h"

using namespace std;

stringstream ss_;

wstring makeCounter(const wchar_t* computer, const wchar_t* object, const wchar_t* instance, const wchar_t* counter);
wstring utfToWideChar(const string& str);
string wideCharToUtf(const wstring& wstr);
vector<wchar_t> vectorToWideChar(const vector<wstring>& files);
vector<wstring> getComputers(const PDH_HLOG phDataSource);
vector<wstring> pdhListToVector(const vector<wchar_t>& v_wchar_t);
SYSTEMTIME longLongToSystemtime(LONGLONG ll);
LONGLONG fileTimeToLongLong(const FILETIME& fileTime);
LONGLONG systemtimeToLongLong(const SYSTEMTIME& time);
string systemtimeToJson(const SYSTEMTIME& time);
SYSTEMTIME stringToSystemtime(const string& str);
double distanceBetweenPoints(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points);
void printSystemtime(const SYSTEMTIME& time);
int dayOfWeek(unsigned int year, unsigned int month, unsigned int day);
double getScale(double max_value, double max_scale_value);

PerfCounters::PerfCounters(PDH_HLOG phDataSource) :
    phDataSource_(phDataSource) {}

void PerfCounters::read() {
    DWORD  pcchBufferSize = 0;
    PdhEnumMachinesHW(phDataSource_, NULL, &pcchBufferSize);
    vector<wchar_t> v_wchar_t(pcchBufferSize);
    PdhEnumMachinesHW(phDataSource_, &v_wchar_t[0], &pcchBufferSize);

    wchar_t* start = &v_wchar_t[0];
    for (wchar_t* p = &v_wchar_t[0]; p < &v_wchar_t.back(); ++p) {
        if (*p == L'\000') {
            computers_.push_back(PerfCountersComp(phDataSource_, wstring(start)));
            computers_.back().read();
            start = p + 1;
        }
    }
}

PerfCountersComp::PerfCountersComp(PDH_HLOG phDataSource, std::wstring computer) :
    phDataSource_(phDataSource),
    computer_(computer) {}

void PerfCountersComp::read() {
    DWORD  pcchBufferSize = 0;
    PdhEnumObjectsHW(phDataSource_, computer_.c_str(), NULL, &pcchBufferSize, PERF_DETAIL_WIZARD, TRUE);
    vector<wchar_t> v_wchar_t(pcchBufferSize);
    PdhEnumObjectsHW(phDataSource_, computer_.c_str(), &v_wchar_t[0], &pcchBufferSize, PERF_DETAIL_WIZARD, TRUE);

    wchar_t* start = &v_wchar_t[0];
    for (wchar_t* p = &v_wchar_t[0]; p < &v_wchar_t.back(); ++p) {
        if (*p == L'\000') {
            objects_.push_back(PerfCountersObject(phDataSource_, computer_, wstring(start)));
            objects_.back().read();
            start = p + 1;
        }
    }
}

PerfCountersObject::PerfCountersObject(PDH_HLOG phDataSource, std::wstring computer, std::wstring object) :
    phDataSource_(phDataSource),
    computer_(computer),
    object_(object) {}

void PerfCountersObject::read() {
    DWORD pcchCounterListLength = 0;
    DWORD pcchInstanceListLength = 0;
    PdhEnumObjectItemsHW(phDataSource_, computer_.c_str(), object_.c_str(), NULL, &pcchCounterListLength, NULL, &pcchInstanceListLength, PERF_DETAIL_WIZARD, 0);

    vector<wchar_t> mszCounterList(pcchCounterListLength);
    vector<wchar_t> mszInstanceList;
    if (pcchInstanceListLength) {
        mszInstanceList.resize(pcchInstanceListLength);
        PdhEnumObjectItemsHW(phDataSource_, computer_.c_str(), object_.c_str(),
            &mszCounterList[0], &pcchCounterListLength,
            &mszInstanceList[0], &pcchInstanceListLength,
            PERF_DETAIL_WIZARD, 0);
        counters_ = pdhListToVector(mszCounterList);
        instances_ = pdhListToVector(mszInstanceList);
    }
    else {
        PdhEnumObjectItemsHW(phDataSource_, computer_.c_str(), object_.c_str(),
            &mszCounterList[0], &pcchCounterListLength,
            NULL, &pcchInstanceListLength,
            PERF_DETAIL_WIZARD, 0);
        counters_ = pdhListToVector(mszCounterList);
        instances_.clear();
    }
}

PerfLogsReader::PerfLogsReader() :
    phDataSource_(nullptr),
    phQuery_(nullptr),
    start_time_({ 0, 0 }),
    end_time_({ 0, 0 }) {}

PerfLogsReader::~PerfLogsReader() {
    close();
}

wstring PerfLogsReader::executeCommandW(const string& cmd) {
    return utfToWideChar(executeCommand(cmd));
}

string PerfLogsReader::executeCommand(const string& cmd) {
    namespace json = boost::json;
    error_code ec;
    json::value jv = json::parse(cmd, ec);
    if (ec) {
        wcout << "Parsing failed: " << utfToWideChar(ec.message()) << '\n';
    }

    if (json::object* j_object = jv.if_object()) {
        string cmd(j_object->at("cmd").if_string()->c_str());
        if (cmd == "open") {
            return executeCommandOpen(j_object->at("files").if_array());
        }
        else if (cmd == "read") {
            return executeCommandRead();
        }
        else if (cmd == "get_values") {
            return executeCommandGetValues(j_object);
        }
    }

    return "";
}

string PerfLogsReader::executeCommandOpen(const boost::json::array* j_array) {
    namespace json = boost::json;
    vector<wstring> files(j_array->size());
    auto it_files = files.begin();
    for (auto it = j_array->begin(); it < j_array->end(); ++it) {
        *it_files = utfToWideChar(string(it->as_string().c_str()));
        ++it_files;
    }

    json::object j_response;
    if (open(files)) {
        j_response.emplace("status", true);
    }
    else {
        j_response.emplace("status", false);
        j_response.emplace("error", wideCharToUtf(message_error_));
    }
    return json::serialize(j_response);
}

string PerfLogsReader::executeCommandRead() {
    namespace json = boost::json;
    json::object j_response;
    if (read()) {
        json::object j_data;
        j_data.emplace("start_time", systemtimeToJson(start_time_));
        j_data.emplace("end_time", systemtimeToJson(end_time_));
        j_data.emplace("counters", countersToJsonObject());

        j_response.emplace("status", true);
        j_response.emplace("data", j_data);
    }
    else {
        j_response.emplace("status", false);
        j_response.emplace("error", wideCharToUtf(message_error_));
    }
    return json::serialize(j_response);
}

string PerfLogsReader::executeCommandGetValues(boost::json::object* j_cmd) {
    namespace json = boost::json;
    SYSTEMTIME start_time = stringToSystemtime(string(j_cmd->at("start_time").if_string()->c_str()));
    SYSTEMTIME end_time = stringToSystemtime(string(j_cmd->at("end_time").if_string()->c_str()));
    uint64_t points = json::value_to<uint64_t>(j_cmd->at("points"));

    vector<Sample> samples = getValues(start_time, end_time, points);

    json::object j_response;
    if (!samples.size() && !message_error_.empty()) {
        j_response.emplace("status", false);
        j_response.emplace("error", wideCharToUtf(message_error_));
        return  json::serialize(j_response);
    }

    json::array j_counters_stat;
    for (auto it = counters_.begin(); it < counters_.end(); ++it) {
        json::object j_counter_stat;
        j_counter_stat.emplace("max", it->max_value_);
        j_counter_stat.emplace("sum", it->sum_value_);
        j_counter_stat.emplace("count", it->count_value_);
        if (it->count_value_) {
            double avg = it->sum_value_ / it->count_value_;
            j_counter_stat.emplace("avg", avg);
        }
        else {
            j_counter_stat.emplace("avg", 0);
        }
        j_counters_stat.push_back(j_counter_stat);
    }

    json::array j_samples;
    json::array j_points;
    for (auto it_sample = samples.begin(); it_sample < samples.end(); ++it_sample) {
        json::array j_values;
        for (size_t index = 0; index < it_sample->values_.size(); ++index) {
            j_values.push_back(it_sample->values_[index]);
        }
        j_samples.push_back(j_values);
        j_points.push_back(systemtimeToJson(it_sample->point_time_).c_str());
    }

    j_response.emplace("status", true);
    j_response.emplace("counters_stat", j_counters_stat);
    j_response.emplace("points", j_points);
    j_response.emplace("samples", j_samples);

    return  json::serialize(j_response);
}

bool PerfLogsReader::open(const vector<wstring>& files) {

    close();

    if (files.size() > 32) {
        message_error_ = L"Можно открыть не более 32 файлов одновременно!";
        return false;
    }

    vector<wchar_t> logFileNameList = move(vectorToWideChar(files));

    //Формируем указатель на источник файлов логов
    PDH_STATUS pdhStatus = PdhBindInputDataSourceW(&phDataSource_, &logFileNameList[0]);
    if (pdhStatus != ERROR_SUCCESS) {
        messageErrorPdh(pdhStatus);
        return false;
    }

    return true;
}

void PerfLogsReader::close() {
    eng_counters_map_.clear();
    national_index_counters_map_.clear();
    counters_.clear();
    perfCounters_ = nullptr;
    if (phQuery_) {
        PdhCloseQuery(phQuery_);
        phQuery_ = nullptr;
    }
    if (phDataSource_) {
        PdhCloseLog(phDataSource_, PDH_FLAGS_CLOSE_QUERY);
        phDataSource_ = nullptr;
    }
}

bool PerfLogsReader::read() {
    if (phDataSource_) {
        perfCounters_ = make_unique<PerfCounters>(phDataSource_);
        perfCounters_->read();

        DWORD pdwNumEntries = 0;
        PDH_TIME_INFO pInfo;
        DWORD pdwBufferSize = sizeof(PDH_TIME_INFO);
        PDH_STATUS pdhStatus = PdhGetDataSourceTimeRangeH(phDataSource_, &pdwNumEntries, &pInfo, &pdwBufferSize);
        if (pdhStatus != ERROR_SUCCESS) {
            messageErrorPdh(pdhStatus);
            return false;
        }

        start_time_ = longLongToSystemtime(pInfo.StartTime);
        end_time_ = longLongToSystemtime(pInfo.EndTime);

        fillNationalIndicesFromRegistry();
        fillEngCountersFromRegistry();
        fillCounters();
        createQuery();

        return true;
    }
    else {
        message_error_ = L"Файлы не открыты!";
        return false;
    }
}

bool PerfLogsReader::createQuery() {
    PDH_STATUS pdhStatus = PdhOpenQueryH(phDataSource_, 0, &phQuery_);
    if (pdhStatus != ERROR_SUCCESS) {
        messageErrorPdh(pdhStatus);
        return false;
    }

    for (auto it = counters_.begin(); it < counters_.end(); ++it) {
        pdhStatus = PdhAddCounterW(phQuery_, it->national_name_.c_str(), 0, &it->hCounter_);
        if (pdhStatus != ERROR_SUCCESS) {
            it->hCounter_ = NULL;
        }
    }
    return true;
}

void PerfLogsReader::messageErrorPdh(DWORD dwErrorCode) {
    HANDLE hPdhLibrary = NULL;
    LPWSTR pMessage = NULL;

    wstringstream wss;

    hPdhLibrary = LoadLibrary(L"pdh.dll");
    if (NULL == hPdhLibrary)
    {
        wss << L"LoadLibrary failed with " << GetLastError();
        message_error_ = wss.str();
        return;
    }

    if (!FormatMessage(FORMAT_MESSAGE_FROM_HMODULE |
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        hPdhLibrary,
        dwErrorCode,
        0,
        (LPWSTR)&pMessage,
        0,
        NULL))
    {
        wss << L"Format message failed with " << GetLastError();
        message_error_ = wss.str();
        return;
    }

    wss << L"Formatted message: " << pMessage;
    message_error_ = wss.str();

    LocalFree(pMessage);
}

uint64_t PerfLogsReader::pointsInPeriod(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points) {
    PDH_TIME_INFO pInfo = { systemtimeToLongLong(startTime), systemtimeToLongLong(endTime) , 1 };
    PDH_STATUS pdhStatus = PdhSetQueryTimeRange(phQuery_, &pInfo);
    pdhStatus = PdhCollectQueryData(phQuery_);
    size_t points_count = 0;
    while (ERROR_SUCCESS == pdhStatus) {
        pdhStatus = PdhCollectQueryData(phQuery_);
        if (ERROR_SUCCESS == pdhStatus) {
            ++points_count;
            if (points_count > points) return points_count;
        }
    }
    return points_count;
}

vector<Sample> PerfLogsReader::getValues(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points) {
    if (!phDataSource_) {
        message_error_ = L"Файлы не открыты!";
        return {};
    }

    uint64_t points_in_period_ = pointsInPeriod(startTime, endTime, points);
    if (points > points_in_period_) points = points_in_period_;
    if (points < 2) points = 2;

    double distance = distanceBetweenPoints(startTime, endTime, points);
    double distance_time = distanceBetweenPoints(startTime, endTime, points - 1);
    uint64_t uStartTime = systemtimeToLongLong(startTime);
    vector<Sample> samples(points);
    for (size_t i = 0; i < points; ++i) {
        samples[i].start_period_ = uStartTime + i * distance;
        samples[i].end_period_ = uStartTime + (i + 1) * distance;
        samples[i].point_time_ = longLongToSystemtime(uStartTime + i * distance_time);
        samples[i].values_.resize(counters_.size());
    }

    PDH_TIME_INFO pInfo = { systemtimeToLongLong(startTime), systemtimeToLongLong(endTime) , 1 };
    PDH_STATUS pdhStatus = PdhSetQueryTimeRange(phQuery_, &pInfo);

    pdhStatus = PdhCollectQueryData(phQuery_);

    DWORD lpdwType = 0;
    PDH_RAW_COUNTER pValue;
    PDH_FMT_COUNTERVALUE fmtValue;
    while (ERROR_SUCCESS == pdhStatus) {
        pdhStatus = PdhCollectQueryData(phQuery_);
        if (ERROR_SUCCESS == pdhStatus) {
            for (size_t i = 0; i < counters_.size(); ++i) {
                Counter& counter = counters_[i];
                if (counter.hCounter_) {
                    PDH_STATUS pdhStatusCounterValue = PdhGetRawCounterValue(counter.hCounter_, &lpdwType, &pValue);
                    if (ERROR_SUCCESS == pdhStatusCounterValue && (PDH_CSTATUS_NEW_DATA == pValue.CStatus || PDH_CSTATUS_VALID_DATA == pValue.CStatus)) {
                        if (counter.prevCounter_.CStatus != PDH_CSTATUS_ITEM_NOT_VALIDATED) {
                            pdhStatusCounterValue = PdhCalculateCounterFromRawValue(counter.hCounter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &pValue, &counter.prevCounter_, &fmtValue);
                            if (ERROR_SUCCESS == pdhStatusCounterValue && (PDH_CSTATUS_NEW_DATA == fmtValue.CStatus || PDH_CSTATUS_VALID_DATA == fmtValue.CStatus)) {
                                size_t index = (fileTimeToLongLong(pValue.TimeStamp) - uStartTime) / distance;
                                if (index >= points) index = points - 1;
                                Sample& sample = samples[index];
                                if (fmtValue.doubleValue > sample.values_[i]) {
                                    sample.values_[i] = fmtValue.doubleValue;
                                }
                                if (fmtValue.doubleValue > counter.max_value_) {
                                    counter.max_value_ = fmtValue.doubleValue;
                                }
                                counter.sum_value_ += fmtValue.doubleValue;
                                ++counter.count_value_;
                            }
                        }
                        counter.prevCounter_ = pValue;
                    }
                }
            }
        }
    }
    samples[0].values_ = samples[1].values_;

    return samples;
}

boost::json::object PerfLogsReader::countersToJsonObject() {
    namespace json = boost::json;
    json::object j_counters;
    j_counters.emplace("columns", json::array({
        "national_name", "computer", "object", "instances", "counter",
        "english_name", "computer_eng", "object_eng", "instances_eng", "counter_eng"
        })
    );

    json::array j_counters_rows;
    for (auto it_counter = counters_.begin(); it_counter < counters_.end(); ++it_counter) {
        j_counters_rows.emplace_back(json::array({
            wideCharToUtf(it_counter->national_name_),
            wideCharToUtf(it_counter->computer_),
            wideCharToUtf(it_counter->object_),
            wideCharToUtf(it_counter->instances_),
            wideCharToUtf(it_counter->counter_),
            wideCharToUtf(it_counter->english_name_),
            wideCharToUtf(it_counter->computer_eng_),
            wideCharToUtf(it_counter->object_eng_),
            wideCharToUtf(it_counter->instances_eng_),
            wideCharToUtf(it_counter->counter_eng_)
            })
        );
    }
    j_counters.emplace("rows", j_counters_rows);
    return j_counters;
}

bool PerfLogsReader::fillEngCountersFromRegistry() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib\\009", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    };
    DWORD lpType;
    DWORD lpcbData = 0;
    if (RegQueryValueExW(hKey, L"Counter", NULL, &lpType, NULL, &lpcbData) != ERROR_SUCCESS) {
        return false;
    }
    vector<wchar_t> lpData(lpcbData / sizeof(wchar_t));
    if (RegQueryValueExW(hKey, L"Counter", NULL, &lpType, reinterpret_cast<LPBYTE>(&lpData[0]), &lpcbData) != ERROR_SUCCESS) {
        return false;
    }

    vector<wstring> index_name_counters = pdhListToVector(lpData);
    for (size_t index = 1; index < index_name_counters.size(); ++index) {
        if (index % 2) {
            eng_counters_map_.insert({ _wtol(&index_name_counters[index - 1][0]), index_name_counters[index] });
        }
    }
    return true;
}

bool PerfLogsReader::fillNationalIndicesFromRegistry() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib\\CurrentLanguage", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    };
    DWORD lpType;
    DWORD lpcbData = 0;
    if (RegQueryValueExW(hKey, L"Counter", NULL, &lpType, NULL, &lpcbData) != ERROR_SUCCESS) {
        return false;
    }
    vector<wchar_t> lpData(lpcbData / sizeof(wchar_t));
    if (RegQueryValueExW(hKey, L"Counter", NULL, &lpType, reinterpret_cast<LPBYTE>(&lpData[0]), &lpcbData) != ERROR_SUCCESS) {
        return false;
    }

    vector<wstring> index_name_counters = pdhListToVector(lpData);
    for (size_t index = 1; index < index_name_counters.size(); ++index) {
        if (index % 2) {
            national_index_counters_map_.insert({ index_name_counters[index], _wtol(&index_name_counters[index - 1][0]) });
        }
    }
    return true;
}

const wstring& PerfLogsReader::getEngName(const std::wstring& national_name) {
    auto it_index = national_index_counters_map_.find(national_name);
    if (it_index == national_index_counters_map_.end()) {
        return national_name;
    }

    auto it = eng_counters_map_.find(it_index->second);
    if (it == eng_counters_map_.end()) {
        return national_name;
    }
    return it->second;
}

bool PerfLogsReader::fillCounters() {
    auto& computers = perfCounters_->getComputers();
    for (auto it_computer = computers.begin(); it_computer < computers.end(); ++it_computer) {
        const wchar_t* pComputer = it_computer->getCompName().c_str();
        auto& objects = it_computer->getObjects();
        for (auto it_object = objects.begin(); it_object < objects.end(); ++it_object) {
            const wchar_t* pObject = it_object->getObjName().c_str();
            const wchar_t* pObjectEng = getEngName(it_object->getObjName()).c_str();
            auto& counters = it_object->getCounters();
            for (auto it_counter = counters.begin(); it_counter < counters.end(); ++it_counter) {
                const wchar_t* pCounter = it_counter->c_str();
                const wchar_t* pCounterEng = getEngName(*it_counter).c_str();
                auto& instances = it_object->getInstances();
                if (instances.size()) {
                    for (auto it_instance = instances.begin(); it_instance < instances.end(); ++it_instance) {
                        const wchar_t* pInstances = it_instance->c_str();
                        const wchar_t* pInstancesEng = getEngName(*it_instance).c_str();
                        counters_.push_back({
                                pComputer, pObject, pInstances, pCounter,
                                pComputer, pObjectEng, pInstancesEng, pCounterEng
                            });
                    }
                }
                else {
                    counters_.push_back({
                            pComputer, pObject, NULL, pCounter,
                            pComputer, pObjectEng, NULL, pCounterEng
                        });
                }
            }
        }
    }

    return true;
}

Counter::Counter(
    const wchar_t* computer, const wchar_t* object, const wchar_t* instances, const wchar_t* counter,
    const wchar_t* computer_eng, const wchar_t* object_eng, const wchar_t* instances_eng, const wchar_t* counter_eng) {
    computer_ = computer;
    object_ = object;
    instances_ = (instances) ? instances : L"- - -";
    counter_ = counter;
    computer_eng_ = computer_eng;
    object_eng_ = object_eng;
    instances_eng_ = (instances_eng) ? instances_eng : L"- - -";
    counter_eng_ = counter_eng;
    national_name_ = makeCounter(computer, object, instances, counter);
    english_name_ = makeCounter(computer_eng, object_eng, instances_eng, counter_eng);
    hCounter_ = NULL;
}

wstring  makeCounter(const wchar_t* computer, const wchar_t* object, const wchar_t* instance, const wchar_t* counter) {
    _PDH_COUNTER_PATH_ELEMENTS_W  pdhElement{
        const_cast<wchar_t*>(computer),
        const_cast<wchar_t*>(object),
        const_cast<wchar_t*>(instance),
        NULL,
        NULL,
        const_cast<wchar_t*>(counter)
    };

    DWORD pcchBufferSize = 0;
    PdhMakeCounterPathW(&pdhElement, NULL, &pcchBufferSize, 0);
    wstring wstr(pcchBufferSize, L'\000');
    PdhMakeCounterPathW(&pdhElement, &wstr[0], &pcchBufferSize, 0);

    wstr.pop_back();
    return  wstr;
}

wstring utfToWideChar(const string& str) {
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], count);
    return wstr;
}

string wideCharToUtf(const wstring& wstr) {
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
    return str;
}

vector<wchar_t> vectorToWideChar(const vector<wstring>& files) {
    //Считаем место под wchar_t
    size_t wchar_t_size = 0;
    for (auto it = files.begin(); it < files.end(); ++it) {
        wchar_t_size += (it->size() + 1);
    }
    ++wchar_t_size;

    //Формируем vector<wchar> списка файлов для PdhBindInputDataSourceW
    vector<wchar_t> v_wchar_t(wchar_t_size);
    wchar_t* p = &v_wchar_t[0];
    for (auto it = files.begin(); it < files.end(); ++it) {
        wmemcpy(p, it->c_str(), it->size());
        p += it->size();
        *p = L'\000';
        ++p;
    }
    *p = L'\000';

    return v_wchar_t;
}

vector<wstring> getComputers(const PDH_HLOG phDataSource) {
    DWORD  pcchBufferSize = 0;
    PdhEnumMachinesHW(phDataSource, NULL, &pcchBufferSize);
    vector<wchar_t> v_comps(pcchBufferSize);
    PdhEnumMachinesHW(phDataSource, &v_comps[0], &pcchBufferSize);

    vector<wstring> computers;
    wchar_t* start = &v_comps[0];
    for (wchar_t* p = &v_comps[0]; p < &v_comps.back(); ++p) {
        if (*p == L'\000') {
            computers.push_back(wstring(start));
            start = p + 1;
        }
    }

    return computers;
}

vector<wstring> pdhListToVector(const vector<wchar_t>& v_wchar_t) {
    vector<wstring> v;
    const wchar_t* start = &v_wchar_t[0];
    for (const wchar_t* p = &v_wchar_t[0]; p < &v_wchar_t.back(); ++p) {
        if (*p == L'\000') {
            v.push_back(wstring(start));
            start = p + 1;
            if (*start == L'\000') break;
        }
    }
    return v;
}

SYSTEMTIME longLongToSystemtime(LONGLONG ll) {
    FILETIME ft = { (DWORD)ll, ll >> 32 };
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&ft, &systemTime);
    return systemTime;
}

LONGLONG fileTimeToLongLong(const FILETIME& fileTime) {
    uint64_t uTime;
    memcpy(&uTime, &fileTime, sizeof(uTime));
    return uTime;
}

LONGLONG systemtimeToLongLong(const SYSTEMTIME& time) {
    FILETIME fileTime;
    SystemTimeToFileTime(&time, &fileTime);
    return fileTimeToLongLong(fileTime);
}

string systemtimeToJson(const SYSTEMTIME& time) {
    const auto w2 = setw(2);
    ss_ << setfill('0') << setw(4) << time.wYear << '-' << w2 << time.wMonth << '-' << w2 << time.wDay
        << 'T'
        << w2 << time.wHour << ':' << w2 << time.wMinute << ':' << w2 << time.wSecond;
    string str = ss_.str();
    ss_.str("");
    ss_.clear();
    return str;
}

SYSTEMTIME stringToSystemtime(const string& str) {
    SYSTEMTIME time{};
    WORD a = 0;
    time.wYear = stoi(str.substr(0, 4));
    time.wMonth = stoi(str.substr(5, 2));
    time.wDay = stoi(str.substr(8, 2));
    time.wHour = stoi(str.substr(11, 2));
    time.wMinute = stoi(str.substr(14, 2));
    time.wSecond = stoi(str.substr(17, 2));

    time.wDayOfWeek = dayOfWeek(time.wYear, time.wMonth, time.wDay);

    return time;
}

int dayOfWeek(unsigned int year, unsigned int month, unsigned int day) {
    unsigned int y, c, m, d;
    if (month == 1 || month == 2) {
        c = (year - 1) / 100;
        y = (year - 1) % 100;
        m = month + 12;
        d = day;
    }
    else {
        c = year / 100;
        y = year % 100;
        m = month;
        d = day;
    }
    int week_day = y + y / 4 + c / 4 - 2 * c + 26 * (m + 1) / 10 + d - 1;   // Формула Целера
    week_day = week_day >= 0 ? (week_day % 7) : (week_day % 7 + 7);         // По модулю, когда iWeek отрицательный

    return week_day;
}

double distanceBetweenPoints(const SYSTEMTIME& startTime, const SYSTEMTIME& endTime, uint64_t points) {
    return (systemtimeToLongLong(endTime) - systemtimeToLongLong(startTime)) / (1.0 * points);
}

void printSystemtime(const SYSTEMTIME& time) {
    wcout << time.wYear << L"." << time.wMonth << L"." << time.wDay << L" " << time.wHour << L":" << time.wMinute << L":" << time.wSecond << L"." << time.wMilliseconds << endl;
}

double getScale(double max_value, double max_scale_value) {
    double scale = 1;
    if (max_value == 1 || max_value == 0) {
        return scale;
    }
    else if (max_value > 1) {
        while (max_value / scale > max_scale_value) {
            scale *= 10;
        }
        return 1 / scale;
    }
    else if (max_value > 0 && max_value < 1) {
        while (max_value * scale < max_value) {
            scale *= 10;
        }
        return scale;
    }
}