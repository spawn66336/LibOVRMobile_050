#ifndef MY_SensorGyroDevice
#define MY_SensorGyroDevice

#include "OVR_Device.h"
#include "Kernel/OVR_Atomic.h"
#include "Kernel/OVR_Log.h"
#include "Kernel/OVR_System.h"

#include "Kernel/OVR_Threads.h"
#include "Kernel/OVR_ThreadCommandQueue.h"
#include "OVR_HIDDevice.h"
#include "OVR_DeviceImpl.h"

#include "OVR_Android_DeviceManager.h"
namespace OVR {

class MySensorDeviceFactory: public DeviceFactory {
public:
	static MySensorDeviceFactory &GetInstance();

	// Enumerates devices, creating and destroying relevant objects in manager.
	virtual void EnumerateDevices(EnumerateVisitor& visitor);

	virtual bool MatchVendorProduct(UInt16 vendorId, UInt16 productId) const;
	virtual bool DetectHIDDevice(DeviceManager* pdevMgr,
			const HIDDeviceDesc& desc);
protected:
	DeviceManager* getManager() const {
		return (DeviceManager*) pManager;
	}
};

class MySensorDeviceDesc: public DeviceCreateDesc {
public:
	MySensorDeviceDesc(DeviceFactory* factory, DeviceType type);

	MySensorDeviceDesc(const MySensorDeviceDesc& other);

	virtual DeviceCreateDesc* Clone() const {
		return new MySensorDeviceDesc(*this);
	}

	virtual DeviceBase* NewDeviceInstance();

	//获取设备信息
	virtual bool GetDeviceInfo(DeviceInfo* info) const;

	//设备匹配
	virtual MatchResult MatchDevice(const DeviceCreateDesc& other,
			DeviceCreateDesc** pcandidate) const;

	UInt16 VendorId;
	UInt16 ProductId;
	UInt16 Version;
};

class My_ScensorGyroDevice: public SensorDevice, public DeviceCommon,public Android::DeviceManagerThread::Notifier {
public:

	My_ScensorGyroDevice(DeviceCreateDesc* createDesc, DeviceBase* parent) :
			SensorDevice(), DeviceCommon(createDesc, this, parent) {
		lastTimeStamp = -1;

	}

	virtual bool Initialize(DeviceBase* parent);
	virtual void Shutdown();

	void SetCoordinateFrame(CoordinateFrame coordframe);
	CoordinateFrame GetCoordinateFrame() const;

	// Sets report rate (in Hz) of MessageBodyFrame messages (delivered through MessageHandler::OnMessage call).
	// Currently supported maximum rate is 1000Hz. If the rate is set to 500 or 333 Hz then OnMessage will be
	// called twice or thrice at the same 'tick'.
	// If the rate is  < 333 then the OnMessage / MessageBodyFrame will be called three
	// times for each 'tick': the first call will contain averaged values, the second
	// and third calls will provide with most recent two recorded samples.
	void SetReportRate(unsigned rateHz);
	// Returns currently set report rate, in Hz. If 0 - error occurred.
	// Note, this value may be different from the one provided for SetReportRate. The return
	// value will contain the actual rate.
	unsigned GetReportRate() const;

	// Sets maximum range settings for the sensor described by SensorRange.
	// The function will fail if you try to pass values outside Maximum supported
	// by the HW, as described by SensorInfo.
	// Pass waitFlag == true to wait for command completion. For waitFlag == true,
	// returns true if the range was applied successfully (no HW error).
	// For waitFlag = false, return 'true' means that command was enqueued successfully.
	bool SetRange(const SensorRange& range, bool waitFlag = false);

	// Return the current sensor range settings for the device. These may not exactly
	// match the values applied through SetRange.
	void GetRange(SensorRange* range) const;

	// Return the factory calibration parameters for the IMU
	void GetFactoryCalibration(Vector3f* AccelOffset, Vector3f* GyroOffset,
			Matrix4f* AccelMatrix, Matrix4f* GyroMatrix, float* Temperature);

	// Ported from DK2 feature reports.
	bool SetUUIDReport(const UUIDReport&);
	bool GetUUIDReport(UUIDReport*);

	bool SetTemperatureReport(const TemperatureReport&);
	bool GetAllTemperatureReports(Array<Array<TemperatureReport> >*);

	bool GetGyroOffsetReport(GyroOffsetReport*);

	virtual bool SetFeatureReport(UByte* data, UInt32 length);
	virtual bool GetFeatureReport(UByte* data, UInt32 length);

	virtual void OnEvent(int i, int fd){}

	//用于通知手机自带Sensor事件
	virtual void OnASensorEvent(void* pEv);

protected:
	// Internal
	virtual DeviceCommon* getDeviceCommon() const {
		return (DeviceCommon*) (this);
	}

private:
	
	int64_t lastTimeStamp;

};
}

#endif
