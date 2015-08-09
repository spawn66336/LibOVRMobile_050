/************************************************************************************

Filename    :   OVR_Android_DeviceManager.h
Content     :   Android-specific DeviceManager header.
Created     :   
Authors     :   

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#ifndef OVR_Android_DeviceManager_h
#define OVR_Android_DeviceManager_h

#include "OVR_DeviceImpl.h"

#include <unistd.h>
#include <sys/poll.h>
#include <android/sensor.h>
#include <android/looper.h>

namespace OVR { 
	
	enum {
		SENSOR_TYPE_ACCELEROMETER = 1,
		SENSOR_TYPE_MAGNETIC_FIELD = 2,
		SENSOR_TYPE_ORIENTATION = 3,
		SENSOR_TYPE_GYROSCOPE = 4,
		SENSOR_TYPE_LIGHT = 5,
		SENSOR_TYPE_TEMPERATURE = 7,
		SENSOR_TYPE_PROXIMITY = 8,
		SENSOR_TYPE_GRAVITY = 9,
		SENSOR_TYPE_ROTATION_VECTOR = 11,
		SENSOR_TYPE_GAME_ROTATION_VECTOR = 15,

	};
	
	namespace Android {

class DeviceManagerThread;

//-------------------------------------------------------------------------------------
// ***** Android DeviceManager

class DeviceManager : public DeviceManagerImpl
{
public:
    DeviceManager();
    ~DeviceManager();

    // Initialize/Shutdowncreate and shutdown manger thread.
    virtual bool Initialize(DeviceBase* parent);
    virtual void Shutdown();

    virtual ThreadCommandQueue* GetThreadQueue();
    virtual ThreadId GetThreadId() const;
    virtual int GetThreadTid() const;
    virtual void SuspendThread() const;
    virtual void ResumeThread() const;

    virtual DeviceEnumerator<> EnumerateDevicesEx(const DeviceEnumerationArgs& args);    

    virtual bool  GetDeviceInfo(DeviceInfo* info) const;

    Ptr<DeviceManagerThread> pThread;
};

//-------------------------------------------------------------------------------------
// ***** Device Manager Background Thread

class DeviceManagerThread : public Thread, public ThreadCommandQueue
{
    friend class DeviceManager;
    enum { ThreadStackSize = 64 * 1024 };
public:
    DeviceManagerThread();
    ~DeviceManagerThread();

    virtual int Run();

    // ThreadCommandQueue notifications for CommandEvent handling.
    virtual void OnPushNonEmpty_Locked() { write(CommandFd[1], this, 1); }
    virtual void OnPopEmpty_Locked()     { }

    class Notifier
    {
    public:
        // Called when I/O is received
        virtual void OnEvent(int i, int fd) = 0;

        // Called when timing ticks are updated.
        // Returns the largest number of seconds this function can
        // wait till next call.
        virtual double  OnTicks(double tickSeconds)
        {
            OVR_UNUSED1(tickSeconds);
            return 1000;
        }

		//用于通知手机自带Sensor事件
		virtual void OnASensorEvent( void* pEv )
		{

		}
    };

    // Add I/O notifier
    bool AddSelectFd(Notifier* notify, int fd);
    bool RemoveSelectFd(Notifier* notify, int fd);

	bool AddASensorNotifier(Notifier* notify, int type);
	bool RemoveASensorNotifier(Notifier* notify, int type);

    // Add notifier that will be called at regular intervals.
    bool AddTicksNotifier(Notifier* notify);
    bool RemoveTicksNotifier(Notifier* notify);

    int GetThreadTid() { return DeviceManagerTid; }
    void SuspendThread();
    void ResumeThread();


	void _ProcessSensorData(void* eventQueue , int identifier);

private:
    
    bool threadInitialized() { return CommandFd[0] != 0; }

    pid_t                   DeviceManagerTid;	// needed to set SCHED_FIFO

    // pipe used to signal commands
    int CommandFd[2];

    Array<struct pollfd>    PollFds;
    Array<Notifier*>        FdNotifiers;

	Array<Notifier*>        ASensorNotifiers;
	Array<int>				ASensorPoolTypes;

	Array<ASensorEvent>		SensorEventList;

    Event                   StartupEvent;
    volatile bool           Suspend;

	Vector3f				LastAccel;
	Vector3f				LastGyro;
	Vector3f				LastMag;
	float					LastTemperature;
	int64_t					LastTimeStamp;


    // Ticks notifiers - used for time-dependent events such as keep-alive.
    Array<Notifier*>        TicksNotifiers;


};

}} // namespace Android::OVR

#endif // OVR_Android_DeviceManager_h
