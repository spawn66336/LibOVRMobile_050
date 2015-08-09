/************************************************************************************

Filename    :   OVR_Android_DeviceManager.h
Content     :   Android implementation of DeviceManager.
Created     :   
Authors     :   

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "OVR_Android_DeviceManager.h"

// Sensor & HMD Factories
#include "OVR_LatencyTestDeviceImpl.h"
#include "OVR_SensorDeviceImpl.h"
#include "OVR_Android_HIDDevice.h"
#include "OVR_Android_HMDDevice.h"

#include "Kernel/OVR_Timer.h"
#include "Kernel/OVR_Std.h"
#include "Kernel/OVR_Log.h"

#include <jni.h>
#include "MY_SensorGyroDevice.h"
#include "Kernel/OVR_Alg.h"




#define EVENT_IDEN 23

jobject gRiftconnection;

namespace OVR { namespace Android {

//-------------------------------------------------------------------------------------
// **** Android::DeviceManager

DeviceManager::DeviceManager()
{
}

DeviceManager::~DeviceManager()
{    
}

bool DeviceManager::Initialize(DeviceBase*)
{
    if (!DeviceManagerImpl::Initialize(0))
        return false;

    pThread = *new DeviceManagerThread();
    if (!pThread || !pThread->Start())
        return false;

    // Wait for the thread to be fully up and running.
	//等待线程初始化完毕
    pThread->StartupEvent.Wait();

    // Do this now that we know the thread's run loop.
	//创建HID设备（USB设备）管理器
    HidDeviceManager = *HIDDeviceManager::CreateInternal(this);
         
    pCreateDesc->pDevice = this;
    LogText("OVR::DeviceManager - initialized.\n");
    return true;
}

void DeviceManager::Shutdown()
{   
    LogText("OVR::DeviceManager - shutting down.\n");

    // Set Manager shutdown marker variable; this prevents
    // any existing DeviceHandle objects from accessing device.
    pCreateDesc->pLock->pManager = 0;

    // Push for thread shutdown *WITH NO WAIT*.
    // This will have the following effect:
    //  - Exit command will get enqueued, which will be executed later on the thread itself.
    //  - Beyond this point, this DeviceManager object may be deleted by our caller.
    //  - Other commands, such as CreateDevice, may execute before ExitCommand, but they will
    //    fail gracefully due to pLock->pManager == 0. Future commands can't be enqued
    //    after pManager is null.
    //  - Once ExitCommand executes, ThreadCommand::Run loop will exit and release the last
    //    reference to the thread object.
    pThread->PushExitCommand(false);
    pThread.Clear();

    DeviceManagerImpl::Shutdown();
}

ThreadCommandQueue* DeviceManager::GetThreadQueue()
{
    return pThread;
}

ThreadId DeviceManager::GetThreadId() const
{
    return pThread->GetThreadId();
}

int DeviceManager::GetThreadTid() const
{
    return pThread->GetThreadTid();
}

void DeviceManager::SuspendThread() const
{
    pThread->SuspendThread();
}

void DeviceManager::ResumeThread() const
{
    pThread->ResumeThread();
}

bool DeviceManager::GetDeviceInfo(DeviceInfo* info) const
{
    if ((info->InfoClassType != Device_Manager) &&
        (info->InfoClassType != Device_None))
        return false;
    
    info->Type    = Device_Manager;
    info->Version = 0;
    OVR_strcpy(info->ProductName, DeviceInfo::MaxNameLength, "DeviceManager");
    OVR_strcpy(info->Manufacturer,DeviceInfo::MaxNameLength, "Oculus VR, LLC");
    return true;
}

DeviceEnumerator<> DeviceManager::EnumerateDevicesEx(const DeviceEnumerationArgs& args)
{
	LogText("Call OVR::DeviceManager::EnumerateDevicesEx\n");
    // TBD: Can this be avoided in the future, once proper device notification is in place?
    pThread->PushCall((DeviceManagerImpl*)this,
                      &DeviceManager::EnumerateAllFactoryDevices, true);
	LogText("Finished Call  pThread->PushCall\n");
    DeviceEnumerator<> e = DeviceManagerImpl::EnumerateDevicesEx(args);
	LogText("Finished Call   DeviceEnumerator<> e = DeviceManagerImpl::EnumerateDevicesEx(args)\n");
	return e;
}


//-------------------------------------------------------------------------------------
// ***** DeviceManager Thread 



DeviceManagerThread::DeviceManagerThread()
    : Thread(ThreadStackSize),
      Suspend( false )
{
    int result = pipe(CommandFd);
	OVR_UNUSED( result );	// no warning
    OVR_ASSERT(!result);
	LastTemperature = 0.0f;
	LastTimeStamp = 0;
    AddSelectFd(NULL, CommandFd[0]);
}

DeviceManagerThread::~DeviceManagerThread()
{
    if (CommandFd[0])
    {
        RemoveSelectFd(NULL, CommandFd[0]);
        close(CommandFd[0]);
        close(CommandFd[1]);
    }
}

bool DeviceManagerThread::AddASensorNotifier(Notifier* notify, int type)
{
	ASensorNotifiers.PushBack(notify);
	ASensorPoolTypes.PushBack(type);

	OVR_ASSERT(ASensorNotifiers.GetSize() == ASensorPoolTypes.GetSize());
	LogText("DeviceManagerThread::AddASensorNotifier %d (Tid=%d)\n", type, GetThreadTid());
	return true;
}

bool DeviceManagerThread::RemoveASensorNotifier(Notifier* notify, int type)
{
	for (UPInt i = 0; i < ASensorNotifiers.GetSize(); i++)
	{
		if ((ASensorNotifiers[i] == notify) && (ASensorPoolTypes[i] == type))
		{
			ASensorNotifiers.RemoveAt(i);
			ASensorPoolTypes.RemoveAt(i);
			return true;
		}
	}
	return false;
}


bool DeviceManagerThread::AddSelectFd(Notifier* notify, int fd)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN|POLLHUP|POLLERR;
    pfd.revents = 0;

    FdNotifiers.PushBack(notify);
    PollFds.PushBack(pfd);

    OVR_ASSERT(FdNotifiers.GetSize() == PollFds.GetSize());
    LogText( "DeviceManagerThread::AddSelectFd %d (Tid=%d)\n", fd, GetThreadTid() );
    return true;
}

bool DeviceManagerThread::RemoveSelectFd(Notifier* notify, int fd)
{
    // [0] is reserved for thread commands with notify of null, but we still
    // can use this function to remove it.

    LogText( "DeviceManagerThread::RemoveSelectFd %d (Tid=%d)\n", fd, GetThreadTid() );
    for (UPInt i = 0; i < FdNotifiers.GetSize(); i++)
    {
        if ((FdNotifiers[i] == notify) && (PollFds[i].fd == fd))
        {
            FdNotifiers.RemoveAt(i);
            PollFds.RemoveAt(i);
            return true;
        }
    }
    LogText( "DeviceManagerThread::RemoveSelectFd failed %d (Tid=%d)\n", fd, GetThreadTid() );
    return false;
}

//static int event_count = 0;
//static double event_time = 0;

struct SensorObj
{
	ASensorRef	sensor;
	int			id;
	const char*	name;
	int			type;
	float		resolution;
	int			minDelay;

	SensorObj(ASensorRef ref, int _id)
	{
		sensor = ref;
		id = _id;
		name = ASensor_getName(sensor);
		type = ASensor_getType(sensor);
		resolution = ASensor_getResolution(sensor);
		minDelay = ASensor_getMinDelay(sensor);
	}

	void Enable(ASensorEventQueue* evQueue)
	{
		int succ =  ASensorEventQueue_enableSensor(evQueue, sensor);
		if (succ < 0)
		{
			LogText("DeviceManagerThread - enable Sensor %s Failed !!!!\n", name);
		}
		else{
			ASensorEventQueue_setEventRate(evQueue, sensor, minDelay);
		}
	}

	void Disable(ASensorEventQueue* evQueue)
	{
		int succ = ASensorEventQueue_disableSensor(evQueue, sensor);

		if (succ < 0)
		{
			LogText("DeviceManagerThread - disable Sensor %s Failed !!!!\n", name);
		}
	}

	void PrintInfo()
	{
		LogText("ASensor: name = %s , type = %d , res = %f , delay = %d \n", name , type , resolution , minDelay);
	}

};


int DeviceManagerThread::Run()
{
    ThreadCommand::PopBuffer command;

    SetThreadName("OVR::DeviceMngr");

    LogText( "DeviceManagerThread - running (Tid=%d).\n", GetThreadTid() );
    
    // needed to set SCHED_FIFO
    DeviceManagerTid = gettid();

    ASensorManager* sensorManager = NULL;
    ASensorEventQueue* eventQueue = NULL;
    ALooper* looper = NULL;

    sensorManager = ASensorManager_getInstance();
   
	Array<SensorObj> sensors;

	//获取手机上全部Sensor
	ASensorList sensorList = NULL; 
	int numSensor = ASensorManager_getSensorList(sensorManager, &sensorList);

	for (int i = 0; i < numSensor; i++)
	{
		int type = ASensor_getType(sensorList[i]);
		if (type == SENSOR_TYPE_ACCELEROMETER ||
			type == SENSOR_TYPE_GYROSCOPE ||
			type == SENSOR_TYPE_MAGNETIC_FIELD ||
			type == SENSOR_TYPE_TEMPERATURE
			)
		{		 
			sensors.PushBack(SensorObj(sensorList[i], i));
		}
	}

	//打印全部Sensor信息
	for (UPInt i = 0; i < sensors.GetSize(); i++)
	{
		sensors[i].PrintInfo();
	}

	looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
	looper = ALooper_forThread();

	if( looper == NULL )
	{
		LogText("Failed to create looper!");
	}

	eventQueue = ASensorManager_createEventQueue(sensorManager, looper, EVENT_IDEN, NULL, NULL);
	OVR_UNUSED(looper);
	OVR_UNUSED(eventQueue);

	//启动所有Sensor
	for (UPInt i = 0; i < sensors.GetSize(); i++)
	{
		sensors[i].Enable(eventQueue);
	}
	
    // Signal to the parent thread that initialization has finished.
    StartupEvent.SetEvent();

    while(!IsExiting())
    {
        // PopCommand will reset event on empty queue.
        if (PopCommand(&command))
        {
            command.Execute();
        }
        else
        {
			if(ASensorNotifiers.GetSize() > 0)
			{
				int ident;
				int events;
				struct android_poll_source* source;

				if ((ident = ALooper_pollAll(20, NULL, &events, (void**)&source)) >= 0) 
				{
					_ProcessSensorData(eventQueue,ident);
				}
			}

#if 0
            bool commands = false;
            do
            {
                int waitMs = INT_MAX;

                // If devices have time-dependent logic registered, get the longest wait
                // allowed based on current ticks.
                if (!TicksNotifiers.IsEmpty())
                {
                    double timeSeconds = Timer::GetSeconds();
                    int    waitAllowed;

                    for (UPInt j = 0; j < TicksNotifiers.GetSize(); j++)
                    {
                        waitAllowed = (int)(TicksNotifiers[j]->OnTicks(timeSeconds) * Timer::MsPerSecond);
                        if (waitAllowed < (int)waitMs)
                        {
                            waitMs = waitAllowed;
                        }
                    }
                }

                nfds_t nfds = PollFds.GetSize();
                if (Suspend)
                {
                    // only poll for commands when device polling is suspended
                    nfds = Alg::Min(nfds, (nfds_t)1);
                    // wait no more than 100 milliseconds to allow polling of the devices to resume
                    // within 100 milliseconds to avoid any noticeable loss of head tracking
                    waitMs = Alg::Min(waitMs, 100);
                }

                // wait until there is data available on one of the devices or the timeout expires
                int n = poll(&PollFds[0], nfds, waitMs);

                if (n > 0)
                {
                    // Iterate backwards through the list so the ordering will not be
                    // affected if the called object gets removed during the callback
                    // Also, the HID data streams are located toward the back of the list
                    // and servicing them first will allow a disconnect to be handled
                    // and cleaned directly at the device first instead of the general HID monitor
                    for (int i = nfds - 1; i >= 0; i--)
                    {
                    	const short revents = PollFds[i].revents;

                    	// If there was an error or hangup then we continue, the read will fail, and we'll close it.
                    	if (revents & (POLLIN | POLLERR | POLLHUP))
                        {
                            if ( revents & POLLERR )
                            {
                                LogText( "DeviceManagerThread - poll error event %d (Tid=%d)\n", PollFds[i].fd, GetThreadTid() );
                            }
                            if (FdNotifiers[i])
                            {
                                event_count++;
                                if ( event_count >= 500 )
                                {
                                    const double current_time = Timer::GetSeconds();
                                    const int eventHz = (int)( event_count / ( current_time - event_time ) + 0.5 );
                                    LogText( "DeviceManagerThread - event %d (%dHz) (Tid=%d)\n", PollFds[i].fd, eventHz, GetThreadTid() );
                                    event_count = 0;
                                    event_time = current_time;
                                }

                                FdNotifiers[i]->OnEvent(i, PollFds[i].fd);
                            }
                            else if (i == 0) // command
                            {
                                char dummy[128];
                                read(PollFds[i].fd, dummy, 128);
                                commands = true;
                            }
                        }

                        if (revents != 0)
                        {
                            n--;
                            if (n == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                else
                {
                    if ( waitMs > 1 && !Suspend )
                    {
                        LogText( "DeviceManagerThread - poll(fds,%d,%d) = %d (Tid=%d)\n", nfds, waitMs, n, GetThreadTid() );
                    }
                }
            } while (PollFds.GetSize() > 0 && !commands);
#endif

        }
    }

    LogText( "DeviceManagerThread - exiting (Tid=%d).\n", GetThreadTid() );
    return 0;
}

bool DeviceManagerThread::AddTicksNotifier(Notifier* notify)
{
     TicksNotifiers.PushBack(notify);
     return true;
}

bool DeviceManagerThread::RemoveTicksNotifier(Notifier* notify)
{
    for (UPInt i = 0; i < TicksNotifiers.GetSize(); i++)
    {
        if (TicksNotifiers[i] == notify)
        {
            TicksNotifiers.RemoveAt(i);
            return true;
        }
    }
    return false;
}

void DeviceManagerThread::SuspendThread()
{
    Suspend = true;
    LogText( "DeviceManagerThread - Suspend = true\n" );
}

void DeviceManagerThread::ResumeThread()
{
    Suspend = false;
    LogText( "DeviceManagerThread - Suspend = false\n" );
}

void DeviceManagerThread::_ProcessSensorData(void* eventQueue ,int identifier)
{
	if (identifier != EVENT_IDEN){
		return;
	}

	int eventCount = 0;
	OVR_UNUSED(eventCount);
	ASensorEvent event;
	while (ASensorEventQueue_getEvents((ASensorEventQueue*)eventQueue, &event, 1) > 0)
	{
		SensorEventList.PushBack(event);
	}

	//int64_t currTimeStamp = 0x7fffffffffffffff;
	for (UPInt i = 0; i < SensorEventList.GetSize(); i++)
	{
		ASensorEvent ev = SensorEventList.At(i);
		if (ev.type == SENSOR_TYPE_ACCELEROMETER)
		{
			LastAccel.x = ev.vector.x;
			LastAccel.y = ev.vector.y;
			LastAccel.z = ev.vector.z;
		}
		else if (ev.type == SENSOR_TYPE_MAGNETIC_FIELD)
		{
			LastMag.x = ev.vector.x;
			LastMag.y = ev.vector.y;
			LastMag.z = ev.vector.z;
		}
		else if (ev.type == SENSOR_TYPE_GYROSCOPE)
		{
			LastGyro.x = ev.vector.x;
			LastGyro.y = ev.vector.y;
			LastGyro.z = ev.vector.z;
		}
		else if (ev.type == SENSOR_TYPE_TEMPERATURE)
		{
			LastTemperature = ev.temperature;
		}
		 
		LastTimeStamp = ev.timestamp;
		 
		ASensorMessage msg;
		msg.accel = LastAccel;
		msg.gyro = LastGyro;
		msg.mag = LastMag;
		msg.temperature = LastTemperature;
		msg.timestamp = LastTimeStamp;

		for (UPInt j = 0; j < ASensorNotifiers.GetSize(); j++)
		{
			if (ASensorPoolTypes[j] == event.type)
			{
				ASensorNotifiers[j]->OnASensorEvent(&msg);
			}
		}
	}

	
	 
	SensorEventList.Clear();

	//while (SensorEventList.GetSize() > 0)
	//{
	//	int eventTypes[4] = { 
	//		SENSOR_TYPE_ACCELEROMETER, 
	//		SENSOR_TYPE_MAGNETIC_FIELD, 
	//		SENSOR_TYPE_GYROSCOPE,
	//		SENSOR_TYPE_TEMPERATURE
	//	}; 
	//	int indexArray[4] = { 0 };
	//	UPInt j = 0;
	//	for (UPInt i = 0; i < SensorEventList.GetSize(); i++)
	//	{
	//		ASensorEvent ev = SensorEventList.At(i);
	//		if (eventTypes[j] == ev.type)
	//		{
	//			indexArray[j++] = i;
	//			if (j == 4)
	//				break; 
	//			i = 0;
	//		}
	//	}

	//	if (j != 4)
	//	{//若没有同时集其4个传感器信息
	//		break;
	//	}

	//	ASensorMessage msg;
	//	msg.timestamp = 0x7fffffffffffffff;
	//	for (UPInt i = 0; i < 4; i++)
	//	{
	//		ASensorEvent ev = SensorEventList.At(indexArray[i]);
	//		if (ev.type == SENSOR_TYPE_ACCELEROMETER)
	//		{
	//			msg.accel.x = ev.vector.x;
	//			msg.accel.y = ev.vector.y;
	//			msg.accel.z = ev.vector.z;
	//		}
	//		else if (ev.type == SENSOR_TYPE_MAGNETIC_FIELD)
	//		{
	//			msg.mag.x = ev.vector.x;
	//			msg.mag.y = ev.vector.y;
	//			msg.mag.z = ev.vector.z;
	//		}
	//		else if (ev.type == SENSOR_TYPE_GYROSCOPE)
	//		{
	//			msg.gyro.x = ev.vector.x;
	//			msg.gyro.y = ev.vector.y;
	//			msg.gyro.z = ev.vector.z;
	//		}
	//		else if (ev.type == SENSOR_TYPE_TEMPERATURE)
	//		{
	//			msg.temperature = ev.temperature;
	//		}

	//		//时间戳取最小者
	//		msg.timestamp = OVR::Alg::Min(msg.timestamp, ev.timestamp);
	//	}

	//	for (UPInt i = 0; i < ASensorNotifiers.GetSize(); i++)
	//	{
	//		if (ASensorPoolTypes[i] == event.type)
	//		{
	//			ASensorNotifiers[i]->OnASensorEvent(&msg);
	//		}
	//	}

	//	//移除已处理事件
	//	for (int i = 0; i < 4; i++)
	//	{
	//		SensorEventList.RemoveAt(indexArray[i]);
	//	}  
	//}//end of while (SensorEventList.GetSize() > 0)
	  
}


} // namespace Android


//-------------------------------------------------------------------------------------
// ***** Creation


// Creates a new DeviceManager and initializes OVR.
DeviceManager* DeviceManager::Create()
{
    if (!System::IsInitialized())
    {
        // Use custom message, since Log is not yet installed.
        OVR_DEBUG_STATEMENT(Log::GetDefaultLog()->
            LogMessage(Log_Debug, "DeviceManager::Create failed - OVR::System not initialized"); );
        return 0;
    }

    Ptr<Android::DeviceManager> manager = *new Android::DeviceManager;

    if (manager)
    {
        if (manager->Initialize(0))
        {
            manager->AddFactory(&LatencyTestDeviceFactory::GetInstance());
            //manager->AddFactory(&SensorDeviceFactory::GetInstance());
			manager->AddFactory(&MySensorDeviceFactory::GetInstance());
            manager->AddFactory(&Android::HMDDeviceFactory::GetInstance());

            manager->AddRef();
        }
        else
        {
            manager.Clear();
        }

    }    

    return manager.GetPtr();
}


} // namespace OVR

