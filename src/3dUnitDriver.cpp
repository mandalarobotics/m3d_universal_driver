#include "3dUnitDriver.hpp"
#include "encoder_driver.hpp"

#include <boost/log/expressions.hpp>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define M_PI 3.14159265358979323846
#include <logger.hpp>
#include "3dUnitTypeSerialization.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

using namespace m3d;
SET_DEBUGLVL(_PRINTOUT_INFO);



void m3d::transferPc(m3d::rawPointcloud &raw, m3d::pointcloud &normalPc, m3d::_3dUnitConfig &cfg)
{


	for (int i=0; i< raw.angles.size(); i++)
	{

		float ang = raw.angles[i];

		if (ang !=-1)
		{
			glm::mat4 affine3Dunit = glm::rotate(glm::mat4(1.0f),cfg.angularOffsetUnitLaser+ang, glm::vec3(0.0f, 0.0f, 1.0f));


			std::vector<lms_measurement>::iterator profile = raw.ranges.begin()+i;
			for (int i =0; i <profile->echoes[0].data.size(); i++)
			{

				//float lasAng = float(1.0*i *(lit->profile.echoes[0].angStepWidth)  -135.0f);
				//float lasAng = float(1.0*i *(profile->echoes[0].angStepWidth)  - profile->echoes[0].startAngle);
				float lasAng = float(1.0*i *(profile->echoes[0].angStepWidth)+cfg.angularOffsetRotLaser);

				float d = profile->echoes[0].data[i];
				glm::vec4 in (d, 0.0, 0.0f, 1.0f);
				glm::mat4 affineLaser = glm::rotate(glm::mat4(1.0f),  glm::radians(lasAng),glm::vec3(0.0f, 0.0f, 1.0f));
				glm::mat4 calib = cfg.calibMatrix;
				glm::mat4 cor = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),glm::vec3(0.0f, 1.0f, 0.0f));

				//glm::mat4 dAffine = glm::matrixCompMult(affineLaser, affine);
				glm::vec4 out =affine3Dunit* cor* calib* affineLaser * in;
				normalPc.data.push_back(glm::vec3(out.x, out.y,out.z));		
				if (profile->rssis.size()>0)normalPc.intensity.push_back(profile->rssis[0].data[i]);
			}
		}
	}
}

bool _3dUnitConfig::readConfigFromXML(std::string fileName)
{
	LOG_INFO("config file       :" << fileName);
	using boost::property_tree::ptree;
	ptree pt;
	try {
		read_xml(fileName,pt);
	}
	catch(std::exception const&  e)
	{
		LOG_FATAL("cannot parse file "<< e.what());
		exit(-1);
	}

    try {
	
		maximumAngleOfScan = 1.0f*M_PI;
		angularOffsetRotLaser =0.0f;
		angularOffsetUnitLaser=0.0f;
		boost::optional<float> op_maximumAngleOfScan = pt.get_optional<float>("m3dUnitDriver.angles.maximum");
		boost::optional<float> op_angularOffsetRotLaser = pt.get_optional<float> ("m3dUnitDriver.angles.laserOffset");
		boost::optional<float> op_angularOffsetUnitLaser = pt.get_optional<float> ("m3dUnitDriver.angles.unitOffset");

		boost::optional<std::string> op_outputPath = pt.get_optional<std::string> ("m3dUnitDriver.outputFolder");
		outputPath ="";
		if (op_outputPath) outputPath = *op_outputPath;


		if(op_maximumAngleOfScan) maximumAngleOfScan = *op_maximumAngleOfScan;
		if(op_angularOffsetRotLaser) angularOffsetRotLaser =*op_angularOffsetRotLaser;
		if(op_angularOffsetUnitLaser) angularOffsetUnitLaser =*op_angularOffsetUnitLaser;

		rotLaserIp = pt.get<std::string>("m3dUnitDriver.adresses.rotLaser");
        unitIp= pt.get<std::string> ("m3dUnitDriver.adresses.unit");
		ptree locpt = pt.get_child("m3dUnitDriver");
        m3d::typeSerialization::deserialize(locpt, calibMatrix, "calibration");
	
		frontLaserIp = pt.get<std::string>("m3dUnitDriver.adresses.frontLaser");

    }
    catch (...)
    {

    }
    LOG_INFO("Configuration readed");
    LOG_INFO("calibration matrix");
    LOG_INFO("\t"<<calibMatrix[0][0]<<"\t"<<calibMatrix[0][1]<<"\t"<<calibMatrix[0][2]<<"\t"<<calibMatrix[0][3]<<"\t");
    LOG_INFO("\t"<<calibMatrix[1][0]<<"\t"<<calibMatrix[1][1]<<"\t"<<calibMatrix[1][2]<<"\t"<<calibMatrix[1][3]<<"\t");
    LOG_INFO("\t"<<calibMatrix[2][0]<<"\t"<<calibMatrix[2][1]<<"\t"<<calibMatrix[2][2]<<"\t"<<calibMatrix[2][3]<<"\t");
    LOG_INFO("\t"<<calibMatrix[3][0]<<"\t"<<calibMatrix[3][1]<<"\t"<<calibMatrix[3][2]<<"\t"<<calibMatrix[3][3]<<"\t");


	LOG_INFO("front laser IP     :" << frontLaserIp);
	LOG_INFO("roatation laser IP : "<< rotLaserIp);
	LOG_INFO("unit IP            :" << unitIp);
	LOG_INFO("output folder      :" << outputPath);

	return true;

}

void _3dUnitDriver::initializeEncoderOnly()
{
	encThread  =  boost::thread(boost::bind(&_3dUnitDriver::encoderWorker, this));
	applyPriority(&encThread, ABOVE_NORMAL);
	boost::this_thread::sleep(boost::posix_time::milliseconds(500));
}

void _3dUnitDriver::startOver()
{
	LOG_INFO ("Starting over pointcloud");
	LOG_DEBUG("get angle :"<<angleCollection);
	angleCollection = 0.0f;

	pointcloudLock.lock();
	{
		progress = 0;
		collectingPointCloud.data.clear();
		collectingPointCloud.intensity.clear();
		collectingRawpointcloud.angles.clear();
		collectingRawpointcloud.ranges.clear();

	}
	pointcloudLock.unlock();
}
	void _3dUnitDriver::initialize()
{
	newSpeed =-1.0f;
	is_initialized = true;
	_done = false;
	LOG_INFO("initializing _3dUnitDriver");
	lmsThread  =  boost::thread(boost::bind(&_3dUnitDriver::laserThreadWorker, this));
	encThread  =  boost::thread(boost::bind(&_3dUnitDriver::encoderWorker, this));
	collectorThread =  boost::thread(boost::bind(&_3dUnitDriver::combineThread, this));
	applyPriority(&lmsThread, ABOVE_NORMAL);
	applyPriority(&encThread, ABOVE_NORMAL);
	boost::this_thread::sleep(boost::posix_time::milliseconds(500));
}

void _3dUnitDriver::deinitialize()
{
	_done = true;
	is_initialized = false;
	LOG_INFO("waiting for joining threads");
	lmsThread.join();
	encThread.join();
	collectorThread.join();

}

_3dUnitDriver::_3dUnitDriver(_3dUnitConfig config)
{
	is_initialized  = false;
	//LMS.debug();
	lmsIp =config.rotLaserIp;
	unitIp=config.unitIp;
	newSpeed =-1.0f;
    calib = config.calibMatrix;

    LOG_INFO("calibration matrix");
    LOG_INFO("\t"<<calib[0][0]<<"\t"<<calib[0][1]<<"\t"<<calib[0][2]<<"\t"<<calib[0][3]<<"\t");
    LOG_INFO("\t"<<calib[1][0]<<"\t"<<calib[1][1]<<"\t"<<calib[1][2]<<"\t"<<calib[1][3]<<"\t");
    LOG_INFO("\t"<<calib[2][0]<<"\t"<<calib[2][1]<<"\t"<<calib[2][2]<<"\t"<<calib[2][3]<<"\t");
    LOG_INFO("\t"<<calib[3][0]<<"\t"<<calib[3][1]<<"\t"<<calib[3][2]<<"\t"<<calib[3][3]<<"\t");
	maximumScanAngle = config.maximumAngleOfScan;
	angularOffsetRotLaser = config.angularOffsetRotLaser;
	angularOffsetUnitLaser = config.angularOffsetUnitLaser;
	collectingPointcloud=false;
	_newPointCloud=false;
}


void _3dUnitDriver::encoderWorker()
{
	LOG_INFO("enc thread started");
	ENC.connect_to_m3d(unitIp);
	ENC.setSpeed(30);
	while (!_done)
	{
		if (newSpeed !=-1.0f)
		{
			if (ENC.setSpeed((int)newSpeed))	newSpeed =-1;
		}
		bool isAngle =false;
		double angle = ENC.getAngle(isAngle);
		if (isAngle)
		{
			encoderMeasurment m;

			boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
			m.first = now;
			m.second =static_cast<float>(angle);
			LOG_DEBUG("angle:"<< m.second);
			encMeasurmentLock.lock();
			curentAngle = m.second;
			encMeasurmentBuffer.push_back(m);
			if(encMeasurmentBuffer.size()> 10) encMeasurmentBuffer.pop_back();
			encMeasurmentLock.unlock();

		}

		boost::this_thread::sleep(boost::posix_time::millisec(3));
	}
	ENC.setSpeed(0);
	boost::this_thread::sleep(boost::posix_time::millisec(300));
	LOG_INFO("enc thread ended");
}

void _3dUnitDriver::laserThreadWorker()
{
	LOG_INFO("lms thread started");

	LMS.connectToLaser(lmsIp);
	LMS.requestContinousScan();

	while(!_done)
	{
		bool isMeasurment = false;
		LMS.readData(isMeasurment);
		if (isMeasurment &&!LMS.currentMessage.echoes.empty())
		{
			boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();


            profileWithAngle m;
            m.encoder = fabs(curentAngle);
			m.profile =LMS.currentMessage;
			laserMeasurmentsLock.lock();
			readyData.push_back(m);
			laserMeasurmentsLock.unlock();
            boost::this_thread::sleep(boost::posix_time::millisec(5));
		}
	}
	LMS.disconnet();
	LOG_INFO("lms thread ended");
}

void _3dUnitDriver::getPointCloud(pointcloud &pc)
{
	pointcloudLock.lock();
	pc.data = lastPointCloud.data;
	pc.intensity = lastPointCloud.intensity;
	pointcloudLock.unlock();
}


void _3dUnitDriver::getRawPointCloud(rawPointcloud &pc)
{
	pointcloudLock.lock();
	pc.angles = lastRawPointCloud.angles;
	pc.ranges = lastRawPointCloud.ranges;
	pointcloudLock.unlock();
}
void _3dUnitDriver::combineThread()
{

	while (!_done)
	{
		if (readyData.size()< 50)
		{
			boost::this_thread::sleep(boost::posix_time::millisec(100));
			continue;
		}

		std::vector<glm::vec4> pointcloud;
		std::vector<profileWithAngle> copyProfiles;

		laserMeasurmentsLock.lock();
		copyProfiles = readyData;
		readyData.clear();
		laserMeasurmentsLock.unlock();

		for (std::vector<profileWithAngle>::iterator lit = copyProfiles.begin(); lit != copyProfiles.end(); lit++)
		{


			float ang = lit->encoder;

			if (ang !=-1)
			{
				glm::mat4 affine3Dunit = glm::rotate(glm::mat4(1.0f), angularOffsetUnitLaser+ang, glm::vec3(0.0f, 0.0f, 1.0f));



				for (int i =0; i < lit->profile.echoes[0].data.size(); i++)
				{

					//float lasAng = float(1.0*i *(lit->profile.echoes[0].angStepWidth)  -135.0f);
					//float lasAng = float(1.0*i *(lit->profile.echoes[0].angStepWidth)  -lit->profile.echoes[0].startAngle);
					float lasAng = float(1.0*i *(lit->profile.echoes[0].angStepWidth)+angularOffsetRotLaser);

					float d = lit->profile.echoes[0].data[i];
					glm::vec4 in (d, 0.0, 0.0f, 1.0f);
					glm::mat4 affineLaser = glm::rotate(glm::mat4(1.0f), glm::radians(lasAng),glm::vec3(0.0f, 0.0f, 1.0f));
                    //glm::mat4 calib = glm::translate(glm::mat4(1.0f),glm::vec3(0.0f, 0.0f, -50.0f));
					glm::mat4 cor = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),glm::vec3(0.0f, 1.0f, 0.0f));

					//glm::mat4 dAffine = glm::matrixCompMult(affineLaser, affine);
					glm::vec4 out =affine3Dunit* cor* calib* affineLaser * in;
					collectingPointCloud.data.push_back(glm::vec3(out.x, out.y,out.z));		
					if (lit->profile.rssis.size()>0)collectingPointCloud.intensity.push_back( lit->profile.rssis[0].data[i]);
				}

				collectingRawpointcloud.angles.push_back(lit->encoder);
				collectingRawpointcloud.ranges.push_back(lit->profile);

                float dAngle = fabs(lit->encoder-lastAngleCollection);
				//if greater than M_PI or NAN then dangle
				if (dAngle > 0.3*M_PI || (dAngle!=dAngle)) dAngle=0;

				angleCollection=angleCollection+dAngle;


				angleCollection = angleCollection+dAngle;
				progress =-1.0f;
				if (angleCollection !=0.f)
				{
					progress = angleCollection /( 2.2* maximumScanAngle);
				}
				if (fabs(angleCollection) > 2.2* maximumScanAngle)
				{
					LOG_DEBUG("get angle :"<<angleCollection);
					angleCollection = 0.0f;

					pointcloudLock.lock();

					{
						lastPointCloud.data = collectingPointCloud.data;
						lastPointCloud.intensity = collectingPointCloud.intensity;

						lastRawPointCloud.ranges = collectingRawpointcloud.ranges;
						lastRawPointCloud.angles = collectingRawpointcloud.angles;


						collectingPointCloud.data.clear();
						collectingPointCloud.intensity.clear();
						collectingRawpointcloud.angles.clear();
						collectingRawpointcloud.ranges.clear();

					}
					pointcloudLock.unlock();
					_newPointCloud = true;
					if(!scanCallback.empty()) scanCallback();
				}

				lastAngleCollection =  lit->encoder;	
			}
		}
	}
	LOG_INFO("combine thread ended");

}
void m3d::applyPriority(boost::thread* m_pThread,  threadPriority priority)
{
	// some native WINAPI ugh!
#ifdef _WIN32
	if (!m_pThread)
		return;

	BOOL res;
	HANDLE th = m_pThread->native_handle();
	switch (priority)
	{
	case REALTIME		: res = SetThreadPriority(th, THREAD_PRIORITY_TIME_CRITICAL);	break;
	case HIGH			: res = SetThreadPriority(th, THREAD_PRIORITY_HIGHEST);			break;
	case ABOVE_NORMAL	: res = SetThreadPriority(th, THREAD_PRIORITY_ABOVE_NORMAL);	break;
	case NORMAL			: res = SetThreadPriority(th, THREAD_PRIORITY_NORMAL);			break;
	case BELOW_NORMAL	: res = SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);	break;
	case IDLE			: res = SetThreadPriority(th, THREAD_PRIORITY_LOWEST);			break;
	}
#else
	std::cerr<<"CANNOT SET THREAD PRIORITY ON THIS PLATFORM \n";
#endif
}

float _3dUnitDriver::getNearestAngle(boost::posix_time::ptime timeStamp)
{
	///@todo vector is nearly sorted - maybe optimalisation!!
	///@todo maybe better is map for measurments;
	float angle = -1;
	long long int maxTimeD =10;//msec
	long long int timeAcc =2; //msec
	long long int minimal_dTime =maxTimeD;
	for (std::vector<encoderMeasurment>::iterator it = encMeasurmentBuffer.begin(); it!= encMeasurmentBuffer.end(); it++)
	{
		long long int timeD = (it->first-timeStamp).total_milliseconds();
		if (abs(timeD) < minimal_dTime)
		{
			angle = it->second;
			minimal_dTime= abs(timeD);

		}
	}
	return angle;
}
