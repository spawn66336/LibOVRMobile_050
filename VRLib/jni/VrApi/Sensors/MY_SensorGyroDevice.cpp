
#include "Kernel\OVR_Log.h"

#include <android/sensor.h>
#include <android/looper.h>

#include "MY_SensorGyroDevice.h"

#include <android/sensor.h>
#include <android/looper.h>
#include "Kernel/OVR_Math.h"
#include "../../LibOVR/Src/Kernel/OVR_Atomic.h"
#include "../../LibOVR/Src/Kernel/OVR_Timer.h"

namespace OVR
{

	enum
	{
		MyDevice_VendorId = 0x8888,
		MyDevice_ProductId = 0x6666,
		MyDevice_VersionNumber = 0x5555
	};


	const char* MyDevice_SerialNumber = "1234567890";

	void My_ScensorGyroDevice::SetCoordinateFrame(CoordinateFrame coordframe)
	{

	}

	SensorDevice::CoordinateFrame My_ScensorGyroDevice::GetCoordinateFrame() const
	{
		return Coord_Sensor;
	}

	void My_ScensorGyroDevice::SetReportRate(unsigned rateHz)
	{

	}

	unsigned My_ScensorGyroDevice::GetReportRate() const
	{
		return 500;
	}

	bool My_ScensorGyroDevice::SetRange(const SensorRange& range, bool waitFlag /*= false*/)
	{
		return true;
	}

	void My_ScensorGyroDevice::GetRange(SensorRange* range) const
	{

	}

	void My_ScensorGyroDevice::GetFactoryCalibration(Vector3f* AccelOffset, Vector3f* GyroOffset, Matrix4f* AccelMatrix, Matrix4f* GyroMatrix, float* Temperature)
	{

	}

	bool My_ScensorGyroDevice::SetUUIDReport(const UUIDReport&)
	{
		return true;
	}

	bool My_ScensorGyroDevice::GetUUIDReport(UUIDReport*)
	{
		return true;
	}

	bool My_ScensorGyroDevice::SetTemperatureReport(const TemperatureReport&)
	{
		return true;
	}

	bool My_ScensorGyroDevice::GetAllTemperatureReports(Array<Array<TemperatureReport> >*)
	{
		return true;
	}

	bool My_ScensorGyroDevice::GetGyroOffsetReport(GyroOffsetReport*)
	{
		return true;
	}

	bool My_ScensorGyroDevice::SetFeatureReport(UByte* data, UInt32 length)
	{
		return true;
	}

	bool My_ScensorGyroDevice::GetFeatureReport(UByte* data, UInt32 length)
	{
		return true;
	}


	bool My_ScensorGyroDevice::Initialize(DeviceBase* parent)
	{
		LogText("My_ScensorGyroDevice::Initialize()");
		pParent = parent; 
		((OVR::Android::DeviceManager*)GetManager())->pThread->AddASensorNotifier(this, SENSOR_TYPE_GYROSCOPE);
		return true;
	}

	void My_ScensorGyroDevice::Shutdown()
	{
		LogText("My_ScensorGyroDevice::Shutdown()");
	}

	void My_ScensorGyroDevice::OnASensorEvent(void* pEv)
	{
		ASensorMessage* pMsg = (ASensorMessage*)pEv;

		//LogText("SensorMessage - gyro(%f,%f,%f), accel(%f,%f,%f), mag(%f,%f,%f),temp(%f), timestamp(%d) \n",
		//	pMsg->gyro.x, pMsg->gyro.y, pMsg->gyro.z,
		//	pMsg->accel.x, pMsg->accel.y, pMsg->accel.z,
		//	pMsg->mag.x, pMsg->mag.y, pMsg->mag.z,
		//	pMsg->temperature, (int)pMsg->timestamp
		//	);

		if (lastTimeStamp == -1)
		{
			lastTimeStamp = pMsg->timestamp;
		}

		int64_t timeDelta = pMsg->timestamp - lastTimeStamp;
		lastTimeStamp = pMsg->timestamp;

	

		int timeDeltaTicks = (int)(timeDelta / 1000000);
		float timeDeltaSeconds = (float)timeDeltaTicks / 1000.0f;

		LogText("timeDeltaSecs = %f", timeDeltaSeconds);

		if (HandlerRef.GetHandler())
		{
			MessageBodyFrame msg(this);

			msg.AbsoluteTimeSeconds = OVR::Timer::GetSeconds();
			msg.RotationRate = Vector3f( -pMsg->gyro.y , pMsg->gyro.x , pMsg->gyro.z);
			msg.Acceleration = Vector3f( -pMsg->accel.y , pMsg->accel.x , pMsg->accel.z);
			msg.MagneticField = pMsg->mag;
			msg.Temperature = pMsg->temperature;  
			msg.TimeDelta = timeDeltaSeconds;
			HandlerRef.GetHandler()->OnMessage(msg); 

		}
	 
 
	}

	//--------------------------------------------------------------------------

	MySensorDeviceFactory & MySensorDeviceFactory::GetInstance()
	{
		static MySensorDeviceFactory instance;
		return instance;
	}

	void MySensorDeviceFactory::EnumerateDevices(EnumerateVisitor& visitor)
	{
		MySensorDeviceDesc desc(this,Device_Sensor);
		visitor.Visit(desc);
	}

	bool MySensorDeviceFactory::MatchVendorProduct(UInt16 vendorId, UInt16 productId) const
	{
		return vendorId == MyDevice_VendorId && productId == MyDevice_ProductId; 
	}

	bool MySensorDeviceFactory::DetectHIDDevice(DeviceManager* pdevMgr, const HIDDeviceDesc& desc)
	{
		return false;
	}


//---------------------------------------------------------------------------------


	MySensorDeviceDesc::MySensorDeviceDesc(DeviceFactory* factory, DeviceType type)
		: DeviceCreateDesc(factory, type),
		VendorId(MyDevice_VendorId), ProductId(MyDevice_ProductId), Version(MyDevice_VersionNumber)
	{
	}

	MySensorDeviceDesc::MySensorDeviceDesc(const MySensorDeviceDesc& other)
		: DeviceCreateDesc(other.pFactory, other.Type),
		VendorId(MyDevice_VendorId), ProductId(MyDevice_ProductId), Version(MyDevice_VersionNumber)
	{ }

	DeviceBase* MySensorDeviceDesc::NewDeviceInstance()
	{
		return new My_ScensorGyroDevice(this,0);
	}

	bool MySensorDeviceDesc::GetDeviceInfo(DeviceInfo* info) const
	{
		if ((info->InfoClassType != Device_Sensor) &&
			(info->InfoClassType != Device_None))
			return false;

		OVR_strcpy(info->ProductName, DeviceInfo::MaxNameLength, "My Test");
		OVR_strcpy(info->Manufacturer, DeviceInfo::MaxNameLength, "My Test");
		info->Type = Device_Sensor;

		if (info->InfoClassType == Device_Sensor)
		{
			SensorInfo* sinfo = (SensorInfo*)info;
			sinfo->VendorId = MyDevice_VendorId;
			sinfo->ProductId = MyDevice_ProductId;
			sinfo->Version = MyDevice_VersionNumber;
			//sinfo->MaxRanges = SensorRangeImpl::GetMaxSensorRange();
			OVR_strcpy(sinfo->SerialNumber, sizeof(sinfo->SerialNumber), MyDevice_SerialNumber);
		} 

		return true;
	}

	DeviceCreateDesc::MatchResult MySensorDeviceDesc::MatchDevice(const DeviceCreateDesc& other, DeviceCreateDesc** pcandidate) const
	{
		if ((other.Type == Device_Sensor) && (pFactory == other.pFactory))
		{ 
			const MySensorDeviceDesc& o = (const MySensorDeviceDesc&)other;
			if ( 
					o.VendorId == MyDevice_VendorId &&
					o.ProductId == MyDevice_ProductId &&
					o.Version == MyDevice_VersionNumber
				)
				return Match_Found;
		}
		return Match_None;
	}

}
