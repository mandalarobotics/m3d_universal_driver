#ifndef LMS_MINI_LIB_HPP
#define LMS_MINI_LIB_HPP

#include <iostream>
#include <vector>
#include <boost/algorithm/string.hpp>
#include "m3d_driver_lib_export.h"
#include <boost/asio.hpp>
#include <boost/array.hpp>
using boost::asio::ip::tcp;

struct M3D_DRIVER_LIB_EXPORT lms_channel
{
    std::string contents;
    float scallingFactor;
    float scallingOffset;
    float startAngle;
    float angStepWidth;
    int numberOfData;
    std::vector<int>data;
};

struct M3D_DRIVER_LIB_EXPORT lms_measurement
{
    std::string _typefCommand;
    std::string _command;
    int _version;
    int _deviceNo;
    int _serialNo;
    int _deviceStatus[2];
    int _telegramNo;
    int _scanNo;
    int _timeSinceStartUp;
    int _timeOfTransmission;
    int _inputStatus[2];
    int _outputStatus[2];
    int _reserved;
    int _scanningFreq;
    int _measurmentFreq;
    int _noOfEncoders;
    int _noOfDISTChannels;
    int _noOfRssiChannels;
    std::vector<lms_channel> echoes;
    std::vector<lms_channel> rssis;

};

class M3D_DRIVER_LIB_EXPORT lms_socket
{
public:


    inline void debug()
    {
        _debug = true;
    }

    lms_measurement currentMessage;
    /// method request scan in continous mode
    void requestContinousScan();

    /// this method stop contrinous mode scanning
    void RequestStopScan();

    /// start connections (open asio socket)
    void connectToLaser(std::string ip);


    lms_socket();
    ~lms_socket();

    void disconnet();

    void sendMsg(std::string msg);

    void readData(bool &isMeasurment);

private:
    bool processSubMsg(std::string& msg);
    bool _debug;
    int searchForPhase(std::string stringPh,  std::vector<std::string> & data , int beg=0);

    int hexToInt(std::string hex);
    float hexToFloat(std::string hex);
    bool processMeasurment( std::vector<std::string> & data );

    bool procesChannel(lms_channel &ch, std::vector<std::string> & data, std::string channelName);

	size_t lastTelegramSize;
 std::string incommingData;
 std::string softwareBuffer;
 boost::asio::io_service io_service;
 tcp::socket socket;
};

#endif
