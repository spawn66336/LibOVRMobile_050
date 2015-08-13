#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>

#include "OVR_SensorDeviceImpl.h"
#include "Kernel/OVR_Array.h"
#include "Kernel/OVR_Timer.h"
#include "Android/LogUtils.h"
#include <jni.h>



struct usbfs_ctrltransfer {
	/* keep in sync with usbdevice_fs.h:usbdevfs_ctrltransfer */
	uint8_t  bmRequestType;
	uint8_t  bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;

	uint32_t timeout;	/* in milliseconds */

	/* pointer to data */
	void *data;
};

struct usbfs_bulktransfer {
	/* keep in sync with usbdevice_fs.h:usbdevfs_bulktransfer */
	unsigned int ep;
	unsigned int len;
	unsigned int timeout;	/* in milliseconds */

	/* pointer to data */
	void *data;
};



#define IOCTL_USBFS_CONTROL	_IOWR('U', 0, struct usbfs_ctrltransfer)
#define IOCTL_USBFS_BULK		_IOWR('U', 2, struct usbfs_bulktransfer)



namespace OVR {

	
	class MyThread
	{
	public:
		MyThread();
		virtual ~MyThread();

		bool	Create();

		virtual void*		Run() = 0;

	private:

		pthread_t	pid_;
	};

	

	//////////////////////////////////////////////////////////////////////////


	static void* ThreadFunc_s(void* arg)
	{
		MyThread* thread = (MyThread*)arg;

		void* r = thread->Run();

		return r;
	}


	MyThread::MyThread()
	{
	}

	MyThread::~MyThread()
	{
	}


	bool MyThread::Create()
	{
		int err = pthread_create(&pid_, NULL, ThreadFunc_s, this);
		if (err != 0){
			//GLog.LogError("Create thread failed!");
			return false;
		}

		return true;
	}




	int devicefd = -1;
	extern Array<SensorDeviceImpl*>  GSensorDevice;

	class DeviceThread : public MyThread
	{
	public:
		DeviceThread();
		~DeviceThread()	{}

		virtual void*		Run();

	private:

	};


	DeviceThread::DeviceThread()
	{

	}


	int ReadSensorDataFromUsb()
	{
		static int count = 0;
		static double lastTime = 0.0;

		if (devicefd < 0) {
			return 0;
		}
		
		char buffer[128];

		usbfs_bulktransfer bulk;
		bulk.ep = 0x81;			// endpoint 1 / read in
		bulk.data = buffer;
		bulk.len = 128;
		bulk.timeout = 0;

		int r = ioctl(devicefd, IOCTL_USBFS_BULK, &bulk);
		if (r < 0) {
			LogText("ioctl IOCTL_USBFS_BULK error r = %d errno %d", r, errno);
			LogText("thread done!");
			return -1;
		}

		double timeSeconds = Timer::GetSeconds();

		count++;
		if (count >= 500){
			double deltaTime = timeSeconds - lastTime;

			float freq = (float)(count / deltaTime);
			LOG("freq %.2f", freq);

			count = 0;
			lastTime = timeSeconds;
		}

		//LogText("bulk transfer size %d", r);

		if (GSensorDevice.GetSize() > 0)	{

			SensorDeviceImpl* device = GSensorDevice.Back();
			device->OnTicks(timeSeconds);
			device->OnInputReport((UByte*)buffer, r);
		}

		return r;
	}


	void* DeviceThread::Run()
	{
		
		while (ReadSensorDataFromUsb() >= 0)
		{
		}

		return 0;
	}


	static DeviceThread deviceThread;

	extern "C"
	{ 

		JNIEXPORT void JNICALL Java_com_epicgames_ue4_GameActivity_setupUsbDevice(JNIEnv * env, jobject thiz, jint fd)
		{
			devicefd = fd;

			//bool r = deviceThread.Create();
			//if (!r) {
			//	LogText("deviceThread.Create() failed!");
			//}

			//LogText("deviceThread.Create()!!!");
		}

		JNIEXPORT void JNICALL Java_com_epicgames_ue4_GameActivity_PushData
			(JNIEnv * env, jobject obj, jbyteArray data, jint size)
		{
			if (size < 0){
				LogText("data size error %d", size);
				return;
			}

			jbyte buffer[128];
			env->GetByteArrayRegion(data, 0, size, buffer);

			//LogText("read %d %c %c", size, buffer[0], buffer[1]);
			//LogText("Java_com_oculusvr_vrscene_MainActivity_PushData sensor %p", GSensorDevice);
			//LogText("Sensor create %d", GSensorDevice.GetSize());

			double timeSeconds = Timer::GetSeconds();

			if (GSensorDevice.GetSize() > 0)	{

				SensorDeviceImpl* device = GSensorDevice.Back();
				device->OnTicks(timeSeconds);
				device->OnInputReport((UByte*)buffer, size);
			}
		}


	} // extern c



	//void KeepAlive()
	//{
	//	static double lastTime = 0.0;

	//	double time = Timer::GetSeconds();

	//	if (time - lastTime > 3.0) {

	//		SensorKeepAliveImpl skeepAlive(10 * 1000);
	//		skeepAlive.Pack();

	//		struct usbfs_ctrltransfer ctrl;

	//		ctrl.bmRequestType = 0x21;
	//		ctrl.bRequest = 0x09;		// set report
	//		ctrl.wValue = 0x308;		// HID_FEATURE | FEATURE_KEEP_ALIVE
	//		ctrl.wIndex = 0;
	//		ctrl.wLength = skeepAlive.PacketSize;
	//		ctrl.data = skeepAlive.Buffer;
	//		ctrl.timeout = 0;

	//		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
	//		if (r < 0) {
	//			LogText("ioctl error r = %d errno %d", r, errno);
	//		}
	//		else {
	//			LogText("SensorKeepAlive succeeded %d", r);
	//			lastTime = time;
	//		}
	//	}
	//}



	bool UsbSetFeature(UByte* data, uint32_t size)
	{
		if (devicefd < 0) {
			return false;
		}

		UByte reportID = data[0];

		struct usbfs_ctrltransfer ctrl;

		ctrl.bmRequestType = 0x21;			// host to device / type : class / Recipient : interface
		ctrl.bRequest = 0x09;				// set report
		ctrl.wValue = 0x300 | reportID;		// HID_FEATURE | id
		ctrl.wIndex = 0;
		ctrl.wLength = size;
		ctrl.data = data;
		ctrl.timeout = 0;

		LogText("ioctl UsbSetFeature %d", reportID);

		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl UsbSetFeature error r = %d errno %d", r, errno);
			return false;
		}

		return true;
	}

	bool UsbGetFeature(UByte* data, uint32_t size)
	{
		if (devicefd < 0) {
			return false;
		}

		UByte reportID = data[0];

		struct usbfs_ctrltransfer ctrl;

		ctrl.bmRequestType = 0xa1;		// device to host / type : class / Recipient : interface
		ctrl.bRequest = 0x01;			// get report
		ctrl.wValue = 0x300 | reportID;		// HID_FEATURE | id
		ctrl.wIndex = 0;
		ctrl.wLength = size;
		ctrl.data = data;
		ctrl.timeout = 0;
		
		LogText("ioctl UsbGetFeature %d", reportID);

		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl UsbGetFeature error r = %d errno %d", r, errno);
			return false;
		}

		return true;
	}



}// namespace ovr