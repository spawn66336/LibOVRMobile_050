/************************************************************************************

PublicHeader:   OVR.h
Filename    :   OVR_Device.h
Content     :   Definition of HMD-related Device interfaces
Created     :   September 21, 2012
Authors     :   Michael Antonov

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#ifndef OVR_Device_h
#define OVR_Device_h

#include "OVR_DeviceConstants.h"
#include "OVR_DeviceHandle.h"
#include "OVR_DeviceMessages.h"
#include "OVR_HIDDeviceBase.h"

#include "Kernel/OVR_Atomic.h"
#include "Kernel/OVR_RefCount.h"
#include "Kernel/OVR_String.h"
#include "Kernel/OVR_Math.h" 


namespace OVR {

// Declared externally
class Profile;
class ProfileManager; // << Should be renamed for consistency

// Forward declarations
class SensorDevice;
class DeviceCommon;
class DeviceManager;

// MessageHandler is a base class from which users derive to receive messages,
// its OnMessage handler will be called for messages once it is installed on
// a device. Same message handler can be installed on multiple devices.
class MessageHandler
{
    friend class MessageHandlerImpl;
public:
    MessageHandler();
    virtual ~MessageHandler();

    // Returns 'true' if handler is currently installed on any devices.
    bool        IsHandlerInstalled() const;

    // Should be called from derived class destructor to avoid handler
    // being called after it exits.
    void        RemoveHandlerFromDevices();

    // Returns a pointer to the internal lock object that is locked by a
    // background thread while OnMessage() is called.
    // This lock guaranteed to survive until ~MessageHandler.
    Lock*       GetHandlerLock() const;


    virtual void OnMessage(const Message&) { }

    // Determines if handler supports a specific message type. Can
    // be used to filter out entire message groups. The result
    // returned by this function shouldn't change after handler creation.
    virtual bool SupportsMessageType(MessageType) const { return true; }    

private:    
    UPInt Internal[4];
};


//-------------------------------------------------------------------------------------
// ***** DeviceBase

// DeviceBase is the base class for all OVR Devices. It provides the following basic
// functionality:
//   - Reports device type, manager, and associated parent (if any).
//   - Supports installable message handlers, which are notified of device events.
//   - Device objects are created through DeviceHandle::CreateDevice or more commonly
//     through DeviceEnumerator<>::CreateDevice.
//   - Created devices are reference counted, starting with RefCount of 1.
//   - Device is resources are cleaned up when it is Released, although its handles
//     may survive longer if referenced.

class DeviceBase : public NewOverrideBase
{    
    friend class DeviceHandle;  
    friend class DeviceManagerImpl;
public:

    // Enumerating DeviceBase enumerates all devices.
    enum { EnumDeviceType = Device_All };

    virtual ~DeviceBase() { }
    virtual void            AddRef();
    virtual void            Release();
    
    virtual DeviceBase*     GetParent() const;
    virtual DeviceManager*  GetManager() const;  

    virtual void            SetMessageHandler(MessageHandler* handler);
    virtual MessageHandler* GetMessageHandler() const;

    virtual DeviceType      GetType() const;
    virtual bool            GetDeviceInfo(DeviceInfo* info) const;

    // Returns true if device is connected and usable
    // FIXME: this does not work on Android because the device code never sends a HIDDeviceMessage_DeviceAdded
    virtual bool            IsConnected();

    // returns the MessageHandler's lock
    Lock*                   GetHandlerLock() const;
protected:
    // Internal
    virtual DeviceCommon*   getDeviceCommon() const = 0;
};


//-------------------------------------------------------------------------------------
// ***** DeviceInfo

// DeviceInfo describes a device and its capabilities, obtained by calling
// GetDeviceInfo. This base class only contains device-independent functionality;
// users will normally use a derived HMDInfo or SensorInfo classes for more
// extensive device info.

class DeviceInfo
{
public:
    DeviceInfo() : InfoClassType(Device_None), Type(Device_None), Version(0)
    {  ProductName[0] = Manufacturer[0] = 0; }

    enum { MaxNameLength = 32 };
    
    // Type of device for which DeviceInfo is intended.
    // This will be set to Device_HMD for HMDInfo structure, note that this may be
    // different form the actual device type since (Device_None) is valid.
    const DeviceType InfoClassType;
    // Type of device this describes. This must be the same as InfoClassType when
    // InfoClassType != Device_None.
    DeviceType       Type;
    // Name string describing the product: "Oculus Rift DK1", etc.
    char             ProductName[MaxNameLength];    
    char             Manufacturer[MaxNameLength];
    unsigned         Version;
    
protected:
    DeviceInfo(DeviceType type) : InfoClassType(type), Type(type), Version(0)
    { ProductName[0] = Manufacturer[0] = 0; }
    void operator = (const DeviceInfo&) { OVR_ASSERT(0); } // Assignment not allowed.
};


//-------------------------------------------------------------------------------------
// DeviceEnumerationArgs provides device enumeration argumenrs for DeviceManager::EnumerateDevicesEx.
class DeviceEnumerationArgs
{
public:
    DeviceEnumerationArgs(DeviceType enumType, bool availableOnly)
        : EnumType(enumType), AvailableOnly(availableOnly)
    { }

    // Helper; returns true if args match our enumeration criteria.
    bool         MatchRule(DeviceType type, bool available) const
    {
        return ((EnumType == type) || (EnumType == Device_All)) &&
                (available || !AvailableOnly);
    }

protected:    
    DeviceType   EnumType;
    bool         AvailableOnly;
};


// DeviceEnumerator<> is used to enumerate and create devices of specified class,
// it is returned by calling MeviceManager::EnumerateDevices. Initially, the enumerator will
// refer to the first device of specified type. Additional devices can be accessed by
// calling Next().

template<class T = DeviceBase>
class DeviceEnumerator : public DeviceHandle
{
    friend class DeviceManager;
    friend class DeviceManagerImpl;
public:
    DeviceEnumerator()
        : DeviceHandle(), EnumArgs(Device_None, true) { }

    // Next advances enumeration to the next device that first criteria.
    // Returns false if no more devices exist that match enumeration criteria.
    bool    Next()          { return enumerateNext(EnumArgs); }

    // Creates an instance of the device referenced by enumerator; returns null
    // if enumerator does not refer to a valid device or device is unavailable.
    // If device was already created, the same object with incremented ref-count is returned.
    T*      CreateDevice()  { return static_cast<T*>(DeviceHandle::CreateDevice()); }

protected:
    DeviceEnumerator(const DeviceHandle &dev, const DeviceEnumerationArgs& args)
        : DeviceHandle(dev), EnumArgs(args)
    { }

    DeviceEnumerationArgs EnumArgs;
};

//-------------------------------------------------------------------------------------
// ***** DeviceManager

// DeviceManager maintains and provides access to devices supported by OVR, such as
// HMDs and sensors. A single instance of DeviceManager is normally created at
// program startup, allowing devices to be enumerated and created. DeviceManager is
// reference counted and is AddRefed by its created child devices, causing it to
// always be the last object that is released.
//
// Install MessageHandler on DeviceManager to detect when devices are inserted or removed.
//
// The following code will create the manager and its first available HMDDevice,
// and then release it when not needed:
//
//  DeviceManager* manager = DeviceManager::Create();
//  HMDDevice*     hmd = manager->EnumerateDevices<HMDDevice>().CreateDevice();
//
//  if (hmd) hmd->Release();
//  if (manager) manager->Release();


class DeviceManager : public DeviceBase
{
public:
  
    DeviceManager()
    { }

    // DeviceBase implementation.
    virtual DeviceType      GetType() const     { return Device_Manager; }
    virtual DeviceManager*  GetManager() const  { return const_cast<DeviceManager*>(this); }

    // Every DeviceManager has an associated profile manager, which us used to store
    // user settings that may affect device behavior. 
    virtual ProfileManager* GetProfileManager() const = 0;


    // EnumerateDevices enumerates all of the available devices of the specified class,
    // returning an enumerator that references the first device. An empty enumerator is
    // returned if no devices are available. The following APIs are exposed through
    // DeviceEnumerator:
    //   DeviceEnumerator::GetType()        - Check device type. Returns Device_None 
    //                                        if no device was found/pointed to.
    //   DeviceEnumerator::GetDeviceInfo()  - Get more information on device.
    //   DeviceEnumerator::CreateDevice()   - Create an instance of device.
    //   DeviceEnumerator::Next()           - Move onto next device.
    template<class D>
    DeviceEnumerator<D>     EnumerateDevices(bool availableOnly = true)
    {
        // TBD: A cleaner (but less efficient) alternative is though enumeratorFromHandle.
        DeviceEnumerator<> e = EnumerateDevicesEx(DeviceEnumerationArgs((DeviceType)D::EnumDeviceType, availableOnly));
        return *reinterpret_cast<DeviceEnumerator<D>*>(&e);
    }
  
    // EnumerateDevicesEx provides internal implementation for device enumeration, enumerating
    // devices based on dynamically specified DeviceType in DeviceEnumerationArgs.
    // End users should call DeumerateDevices<>() instead.
    virtual DeviceEnumerator<> EnumerateDevicesEx(const DeviceEnumerationArgs& args) = 0;

    // Creates a new DeviceManager. Only one instance of DeviceManager should be created at a time.
    static   DeviceManager* Create();

    // Static constant for this device type, used in template cast type checks.
    enum { EnumDeviceType = Device_Manager };



    // Adds a device (DeviceCreateDesc*) into Devices. Returns NULL, 
    // if unsuccessful or device is already in the list.
    virtual Ptr<DeviceCreateDesc> AddDevice_NeedsLock(const DeviceCreateDesc& createDesc) = 0;

protected:
    DeviceEnumerator<> enumeratorFromHandle(const DeviceHandle& h, const DeviceEnumerationArgs& args)
    { return DeviceEnumerator<>(h, args); }

    DeviceManager* getThis() { return this; }
};



//-------------------------------------------------------------------------------------
// ***** HMDInfo 

// This structure describes various aspects of the HMD allowing us to configure rendering.
//
//  Currently included data:
//   - Physical screen dimensions, resolution, and eye distances.
//     (some of these will be configurable with a tool in the future).
//     These arguments allow us to properly setup projection across HMDs.
//   - DisplayDeviceName for identifying HMD screen; system-specific interpretation.
//
// TBD:
//  - Power on/ off?
//  - Sensor rates and capabilities
//  - Distortion radius/variables    
//  - Screen update frequency
//  - Distortion needed flag
//  - Update modes:
//      Set update mode: Stereo (both sides together), mono (same in both eyes),
//                       Alternating, Alternating scan-lines.

class HMDInfo : public DeviceInfo
{
public:
    // Characteristics of the HMD screen and enclosure
    HmdTypeEnum HmdType;
    Size<int>   ResolutionInPixels;
    Size<float> ScreenSizeInMeters;
    float       ScreenGapSizeInMeters;
    float       CenterFromTopInMeters;
    float       LensSeparationInMeters;

    // Timing & shutter data. All values in seconds.
    struct ShutterInfo
    {
        HmdShutterTypeEnum  Type;
        float               VsyncToNextVsync;                // 1/framerate
        float               VsyncToFirstScanline;            // for global shutter, vsync->shutter open.
        float               FirstScanlineToLastScanline;     // for global shutter, will be zero.
        float               PixelSettleTime;                 // estimated.
        float               PixelPersistence;                // Full persistence = 1/framerate.
    }                   Shutter;

    // Desktop coordinate position of the screen (can be negative; may not be present on all platforms)
    int                 DesktopX;
    int                 DesktopY;
    
    // Windows:
    // "\\\\.\\DISPLAY3", etc. Can be used in EnumDisplaySettings/CreateDC.
    char      DisplayDeviceName[32];
    
    // MacOS:
    long      DisplayId;


    HMDInfo()
        : DeviceInfo(Device_HMD),
          HmdType(HmdType_None),
          ResolutionInPixels(0),
          ScreenSizeInMeters(0.0f),
          ScreenGapSizeInMeters(0.0f),
          CenterFromTopInMeters(0),
          LensSeparationInMeters(0),
          DisplayId(0)
    {
        DesktopX = 0;
        DesktopY = 0;
        DisplayDeviceName[0] = 0;
        Shutter.Type = HmdShutter_LAST;
        Shutter.VsyncToNextVsync = 0.0f;
        Shutter.VsyncToFirstScanline = 0.0f;
        Shutter.FirstScanlineToLastScanline = 0.0f;
        Shutter.PixelSettleTime = 0.0f;
        Shutter.PixelPersistence = 0.0f;
    }

    // Operator = copies local fields only (base class must be correct already)
    void operator = (const HMDInfo& src)
    {        
        HmdType                          = src.HmdType;
        ResolutionInPixels               = src.ResolutionInPixels;      
        ScreenSizeInMeters               = src.ScreenSizeInMeters;
        ScreenGapSizeInMeters            = src.ScreenGapSizeInMeters;
        CenterFromTopInMeters            = src.CenterFromTopInMeters;
        LensSeparationInMeters           = src.LensSeparationInMeters;
        DesktopX                         = src.DesktopX;
        DesktopY                         = src.DesktopY;
        Shutter                          = src.Shutter;
        memcpy(DisplayDeviceName, src.DisplayDeviceName, sizeof(DisplayDeviceName));

        DisplayId                        = src.DisplayId;
    }

    bool IsSameDisplay(const HMDInfo& o) const
    {
        return DisplayId == o.DisplayId &&
               String::CompareNoCase(DisplayDeviceName, 
                                     o.DisplayDeviceName) == 0;
    }

};


// HMDDevice represents an Oculus HMD device unit. An instance of this class
// is typically created from the DeviceManager.
//  After HMD device is created, we its sensor data can be obtained by 
//  first creating a Sensor object and then.

//  TBD:
//  - Configure Sensor
//  - APIs to set On-Screen message, other states?

class HMDDevice : public DeviceBase
{
public:
    HMDDevice()
    { }

    // Static constant for this device type, used in template cast type checks.
    enum { EnumDeviceType = Device_HMD };

    virtual DeviceType      GetType() const   { return Device_HMD; }  

    // Requests the currently used profile. This profile affects the
    // settings reported by HMDInfo. 
    virtual Profile*    GetProfile() const = 0;
    // Obtains the currently used profile name. This is initialized to the default
    // profile name, if any; it can then be changed per-device by SetProfileName.    
    virtual const char* GetProfileName() const = 0;
    // Sets the profile user name, changing the data returned by GetProfileInfo.
    virtual bool        SetProfileName(const char* name) = 0;


    // Disconnects from real HMD device. This HMDDevice remains as 'fake' HMD.
    // SensorDevice ptr is used to restore the 'fake' HMD (can be NULL).
    HMDDevice*  Disconnect(SensorDevice*);
    
    // Returns 'true' if HMD device is a 'fake' HMD (was created this way or 
    // 'Disconnect' method was called).
    bool        IsDisconnected() const;
};


//-------------------------------------------------------------------------------------
// ***** SensorRange & SensorInfo

// SensorRange specifies maximum value ranges that SensorDevice hardware is configured
// to detect. Although this range doesn't affect the scale of MessageBodyFrame values,
// physical motions whose positive or negative magnitude is outside the specified range
// may get clamped or misreported. Setting lower values may result in higher precision
// tracking.
struct SensorRange
{
    SensorRange(float maxAcceleration = 0.0f, float maxRotationRate = 0.0f,
                float maxMagneticField = 0.0f)
        : MaxAcceleration(maxAcceleration), MaxRotationRate(maxRotationRate),
          MaxMagneticField(maxMagneticField)
    { }

    // Maximum detected acceleration in m/s^2. Up to 8*G equivalent support guaranteed,
    // where G is ~9.81 m/s^2.
    // Oculus DK1 HW has thresholds near: 2, 4 (default), 8, 16 G.
    float   MaxAcceleration;  
    // Maximum detected angular velocity in rad/s. Up to 8*Pi support guaranteed.
    // Oculus DK1 HW thresholds near: 1, 2, 4, 8 Pi (default).
    float   MaxRotationRate;
    // Maximum detectable Magnetic field strength in Gauss. Up to 2.5 Gauss support guaranteed.
    // Oculus DK1 HW thresholds near: 0.88, 1.3, 1.9, 2.5 gauss.
    float   MaxMagneticField;
};

// SensorInfo describes capabilities of the sensor device.
class SensorInfo : public DeviceInfo
{
public:
    SensorInfo() : DeviceInfo(Device_Sensor), VendorId(0), ProductId(0)
    {
        SerialNumber[0] = 0;
    }

    // HID Vendor and ProductId of the device.
    UInt16      VendorId;
    UInt16      ProductId;
    // MaxRanges report maximum sensor range values supported by HW.
    SensorRange MaxRanges;
    // Sensor (and display) serial number.
    char        SerialNumber[20];

private:
    void operator = (const SensorInfo&) { OVR_ASSERT(0); } // Assignment not allowed.
};

// Serial Number feature report.
struct SerialReport
{
    static const int SERIAL_NUMBER_SIZE = 12;  // Serial Number size = 12 bytes. (Refer 'Tracker Firmware Specification Section 4.9, Pg 18)

	SerialReport()
        : CommandId(0)
	{
        memset(SerialNumberValue, 0, sizeof(SerialNumberValue));
    }
                
    SerialReport(UInt16 commandId,
                UByte SNo[SERIAL_NUMBER_SIZE])
        :	    CommandId(commandId)
    { 
        for (int i=0; i < SERIAL_NUMBER_SIZE; i++)
        {
            SerialNumberValue[i] = SNo[i];
        }
    }

    UInt16      CommandId;
	UByte	    SerialNumberValue[SERIAL_NUMBER_SIZE];          // See 'Tracker Firmware Specification' document for
													  // a description of Serial Report.
};

// UUID.
struct UUIDReport
{
    static const int UUID_SIZE = 20;

	UUIDReport()
        : CommandId(0)
	{
        memset(UUIDValue, 0, sizeof(UUIDValue));
    }
                
    UUIDReport( UInt16 commandId,
                UByte uuid[UUID_SIZE])
        :	    CommandId(commandId)
    { 
        for (int i=0; i<UUID_SIZE; i++)
        {
            UUIDValue[i] = uuid[i];
        }
    }

    UInt16      CommandId;
	UByte	    UUIDValue[UUID_SIZE];          // See 'DK2 Firmware Specification' document for
                                        // a description of UUID.
};

// Temperature calibration result.
struct TemperatureReport
{
    TemperatureReport()
      :	CommandId(0), Version(0), 
        NumBins(0), Bin(0), NumSamples(0), Sample(0), 
        TargetTemperature(0), ActualTemperature(0),
        Time(0), Offset(0)
    {}

    TemperatureReport(  UInt16 commandId,
                        UByte  version,
                        UByte  numBins,
                        UByte  bin,
                        UByte  numSamples,
                        UByte  sample,
                        double targetTemperature,
                        double actualTemperature,
                        UInt32 time,
                        Vector3d offset)
        :	    CommandId(commandId), Version(version), 
                NumBins(numBins), Bin(bin), NumSamples(numSamples), Sample(sample), 
                TargetTemperature(targetTemperature), ActualTemperature(actualTemperature),
                Time(time), Offset(offset)
    { }

    UInt16      CommandId;
    UByte	    Version;          // See 'DK2 Firmware Specification' document for a description of
    UByte	    NumBins;          // temperature calibration data.
    UByte	    Bin;
    UByte	    NumSamples;
    UByte	    Sample;
    double	    TargetTemperature;
    double	    ActualTemperature;
    UInt32      Time;             // Better hope nobody tries to use this in 2038
    Vector3d    Offset;
};

// Gyro autocalibration result.
struct GyroOffsetReport
{
    enum VersionEnum
    {
        // These are not yet defined.
        Version_NoOffset     = 0,
        Version_ShortAvg     = 1,
        Version_LongAvg      = 2
    };

    GyroOffsetReport()
      :	CommandId(0), Version(Version_NoOffset), 
        Offset(0), Temperature(0)
    {}

    GyroOffsetReport(	UInt16		commandId,
						VersionEnum version,
						Vector3d	offset,
						double		temperature)
		:		CommandId(commandId), Version(version), 
				Offset(offset), Temperature(temperature)
    {}

    UInt16      CommandId;
    VersionEnum Version;
    Vector3d    Offset;
    double      Temperature;
};

//-------------------------------------------------------------------------------------
// ***** SensorDevice

// SensorDevice is an interface to sensor data.
// Install a MessageHandler of SensorDevice instance to receive MessageBodyFrame
// notifications.
//
// TBD: Add Polling API? More HID interfaces?

class SensorDevice : public HIDDeviceBase, public DeviceBase
{
public:
    SensorDevice() 
    { }

    // Static constant for this device type, used in template cast type checks.
    enum { EnumDeviceType = Device_Sensor };

    virtual DeviceType GetType() const   { return Device_Sensor; }

    
    // CoordinateFrame defines whether messages come in the coordinate frame
    // of the sensor device or HMD, which has a different internal sensor.
    // Sensors obtained form the HMD will automatically use HMD coordinates.
    enum CoordinateFrame
    {
        Coord_Sensor = 0,
        Coord_HMD    = 1
    };

    virtual void            SetCoordinateFrame(CoordinateFrame coordframe) = 0;
    virtual CoordinateFrame GetCoordinateFrame() const = 0;

    // Sets report rate (in Hz) of MessageBodyFrame messages (delivered through MessageHandler::OnMessage call). 
    // Currently supported maximum rate is 1000Hz. If the rate is set to 500 or 333 Hz then OnMessage will be 
    // called twice or thrice at the same 'tick'. 
    // If the rate is  < 333 then the OnMessage / MessageBodyFrame will be called three
    // times for each 'tick': the first call will contain averaged values, the second
    // and third calls will provide with most recent two recorded samples.
    virtual void        SetReportRate(unsigned rateHz) = 0;
    // Returns currently set report rate, in Hz. If 0 - error occurred.
    // Note, this value may be different from the one provided for SetReportRate. The return
    // value will contain the actual rate.
    virtual unsigned    GetReportRate() const = 0;

    // Sets maximum range settings for the sensor described by SensorRange.    
    // The function will fail if you try to pass values outside Maximum supported
    // by the HW, as described by SensorInfo.
    // Pass waitFlag == true to wait for command completion. For waitFlag == true,
    // returns true if the range was applied successfully (no HW error).
    // For waitFlag = false, return 'true' means that command was enqueued successfully.
    virtual bool       SetRange(const SensorRange& range, bool waitFlag = false) = 0;

    // Return the current sensor range settings for the device. These may not exactly
    // match the values applied through SetRange.
    virtual void       GetRange(SensorRange* range) const = 0;
 
	// Return the factory calibration parameters for the IMU
    virtual void        GetFactoryCalibration(Vector3f* AccelOffset, Vector3f* GyroOffset,
                                              Matrix4f* AccelMatrix, Matrix4f* GyroMatrix, 
                                              float* Temperature) = 0;
 
	// Ported from DK2 feature reports.
    virtual bool		SetUUIDReport(const UUIDReport&) { return false; }
	virtual bool		GetUUIDReport(UUIDReport*) { return false; }
		
    virtual bool		SetTemperatureReport(const TemperatureReport&) { return false; }
    virtual bool        GetAllTemperatureReports(Array<Array<TemperatureReport> >*) { return false; }

    virtual bool        GetGyroOffsetReport(GyroOffsetReport*) { return false; }	
};

//-------------------------------------------------------------------------------------
// ***** LatencyTestConfiguration
// LatencyTestConfiguration specifies configuration information for the Oculus Latency Tester device.
struct LatencyTestConfiguration
{
    LatencyTestConfiguration(const Color& threshold, bool sendSamples = false)
        : Threshold(threshold), SendSamples(sendSamples) 
    {
    }

    // The color threshold for triggering a detected display change.
    Color    Threshold;
    // Flag specifying whether we wish to receive a stream of color values from the sensor.
    bool        SendSamples;
};

//-------------------------------------------------------------------------------------
// ***** LatencyTestDisplay
// LatencyTestDisplay sets the mode and contents of the Latency Tester LED display.
// See the 'Latency Tester Specification' document for more details.
struct LatencyTestDisplay
{
    LatencyTestDisplay(UByte mode, UInt32 value)
        : Mode(mode), Value(value)
    {
    }

    UByte       Mode;       // The display mode that we wish to select.
    UInt32      Value;      // The value to display.
};

//-------------------------------------------------------------------------------------
// ***** LatencyTestDevice

	// LatencyTestDevice provides an interface to the Oculus Latency Tester which is used to test 'motion to photon' latency.
	class LatencyTestDevice : public HIDDeviceBase, public DeviceBase
	{
	public:
		LatencyTestDevice()
		{ }

		// Static constant for this device type, used in template cast type checks.
		enum { EnumDeviceType = Device_LatencyTester };

		virtual DeviceType GetType() const { return Device_LatencyTester; }

		// Specifies configuration information including the threshold for triggering a detected color change,
		// and a flag to enable a stream of sensor values (typically used for debugging).
		virtual bool SetConfiguration(const LatencyTestConfiguration& configuration, bool waitFlag = false) = 0;

		// Get configuration information from device.
		virtual bool GetConfiguration(LatencyTestConfiguration* configuration) = 0;

		// Used to calibrate the latency tester at the start of a test. Display the specified color on the screen
		// beneath the latency tester and then call this method. Calibration information is lost
		// when power is removed from the device.
		virtual bool SetCalibrate(const Color& calibrationColor, bool waitFlag = false) = 0;

		// Triggers the start of a measurement. This starts the millisecond timer on the device and 
		// causes it to respond with the 'MessageLatencyTestStarted' message.
		virtual bool SetStartTest(const Color& targetColor, bool waitFlag = false) = 0;

		// Used to set the value displayed on the LED display panel.
		virtual bool SetDisplay(const LatencyTestDisplay& display, bool waitFlag = false) = 0;

		virtual DeviceBase* GetDevice() { return this; }
	};

	//用于存储Android手机自带陀螺仪、加速计、温度
	struct ASensorMessage
	{
	public:
		ASensorMessage(): 
		accel(), gyro(), mag(),
		temperature(0.0f), timestamp(0)
		{ 
		} 

		Vector3f accel;
		Vector3f gyro;
		Vector3f mag;
		float temperature; 
		int64_t timestamp;
	};

} // namespace OVR

#endif
