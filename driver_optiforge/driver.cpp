#include <openvr_driver.h>
#include "driverlog.h"
#include "pch.h"
#include <vector>
#include <thread>
#include <chrono>
#include <iostream>
#include <mutex>
#include <cstring>
#include <cmath>

#if defined( _WIN32 )
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

// Minimal shims so the rest of the file can use the Winsock-style names
// on both platforms.
typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
static const int INVALID_SOCKET = -1;
static const int SOCKET_ERROR = -1;
#define closesocket close
#define WSAGetLastError() errno
#define WSACleanup()
#endif

#if defined( _WIN32 )
#define strcasecmp_portable _stricmp
#else
#define strcasecmp_portable strcasecmp
#endif

//TCP/IP settings
const int BUFFER_SIZE = 16;

using namespace vr;


#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#define HMD_DLL_IMPORT extern "C" __declspec( dllimport )
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" 
#else
#error "Unsupported Platform."
#endif

inline HmdQuaternion_t HmdQuaternion_Init(double w, double x, double y, double z)
{
	HmdQuaternion_t quat;
	quat.w = w;
	quat.x = x;
	quat.y = y;
	quat.z = z;
	return quat;
}

inline HmdQuaternion_t HmdQuaternion_Multiply(const HmdQuaternion_t& a, const HmdQuaternion_t& b)
{
	return HmdQuaternion_Init(
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
}

inline HmdQuaternion_t HmdQuaternion_FromAxisAngle(double x, double y, double z, double angle)
{
	double s = sin(angle / 2.0);
	return HmdQuaternion_Init(cos(angle / 2.0), x * s, y * s, z * s);
}

// Rotation part of a 3x4 row-major pose matrix to a quaternion
inline HmdQuaternion_t HmdQuaternion_FromMatrix(const HmdMatrix34_t& m)
{
	HmdQuaternion_t q;
	double trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
	if (trace > 0.0)
	{
		double s = 0.5 / sqrt(trace + 1.0);
		q.w = 0.25 / s;
		q.x = (m.m[2][1] - m.m[1][2]) * s;
		q.y = (m.m[0][2] - m.m[2][0]) * s;
		q.z = (m.m[1][0] - m.m[0][1]) * s;
	}
	else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2])
	{
		double s = 2.0 * sqrt(1.0 + m.m[0][0] - m.m[1][1] - m.m[2][2]);
		q.w = (m.m[2][1] - m.m[1][2]) / s;
		q.x = 0.25 * s;
		q.y = (m.m[0][1] + m.m[1][0]) / s;
		q.z = (m.m[0][2] + m.m[2][0]) / s;
	}
	else if (m.m[1][1] > m.m[2][2])
	{
		double s = 2.0 * sqrt(1.0 + m.m[1][1] - m.m[0][0] - m.m[2][2]);
		q.w = (m.m[0][2] - m.m[2][0]) / s;
		q.x = (m.m[0][1] + m.m[1][0]) / s;
		q.y = 0.25 * s;
		q.z = (m.m[1][2] + m.m[2][1]) / s;
	}
	else
	{
		double s = 2.0 * sqrt(1.0 + m.m[2][2] - m.m[0][0] - m.m[1][1]);
		q.w = (m.m[1][0] - m.m[0][1]) / s;
		q.x = (m.m[0][2] + m.m[2][0]) / s;
		q.y = (m.m[1][2] + m.m[2][1]) / s;
		q.z = 0.25 * s;
	}
	return q;
}

inline void HmdMatrix_SetIdentity(HmdMatrix34_t* pMatrix)
{
	pMatrix->m[0][0] = 1.f;
	pMatrix->m[0][1] = 0.f;
	pMatrix->m[0][2] = 0.f;
	pMatrix->m[0][3] = 0.f;
	pMatrix->m[1][0] = 0.f;
	pMatrix->m[1][1] = 1.f;
	pMatrix->m[1][2] = 0.f;
	pMatrix->m[1][3] = 0.f;
	pMatrix->m[2][0] = 0.f;
	pMatrix->m[2][1] = 0.f;
	pMatrix->m[2][2] = 1.f;
	pMatrix->m[2][3] = 0.f;
}


// keys for use with the settings API
static const char* const k_pch_optiforge_Section = "driver_optiforge";
static const char* const k_pch_optiforge_SerialNumber_String = "serialNumber";
static const char* const k_pch_optiforge_ModelNumber_String = "modelNumber";
static const char* const k_pch_optiforge_WindowX_Int32 = "windowX";
static const char* const k_pch_optiforge_WindowY_Int32 = "windowY";
static const char* const k_pch_optiforge_WindowWidth_Int32 = "windowWidth";
static const char* const k_pch_optiforge_WindowHeight_Int32 = "windowHeight";
static const char* const k_pch_optiforge_RenderWidth_Int32 = "renderWidth";
static const char* const k_pch_optiforge_RenderHeight_Int32 = "renderHeight";
static const char* const k_pch_optiforge_SecondsFromVsyncToPhotons_Float = "secondsFromVsyncToPhotons";
static const char* const k_pch_optiforge_DisplayFrequency_Float = "displayFrequency";
static const char* const k_pch_optiforge_IP = "ip";
static const char* const k_pch_optiforge_Port = "port";
static const char* const k_pch_optiforge_TrackerSerial_String = "trackerSerial";
static const char* const k_pch_optiforge_TrackerOffsetX_Float = "trackerOffsetX";
static const char* const k_pch_optiforge_TrackerOffsetY_Float = "trackerOffsetY";
static const char* const k_pch_optiforge_TrackerOffsetZ_Float = "trackerOffsetZ";
static const char* const k_pch_optiforge_TrackerYaw_Float = "trackerYaw";
static const char* const k_pch_optiforge_TrackerPitch_Float = "trackerPitch";
static const char* const k_pch_optiforge_TrackerRoll_Float = "trackerRoll";

static const double k_flDegToRad = 3.14159265358979323846 / 180.0;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

class CWatchdogDriver_optiforge : public IVRWatchdogProvider
{
public:
	CWatchdogDriver_optiforge()
	{
		m_pWatchdogThread = nullptr;
	}

	virtual EVRInitError Init(vr::IVRDriverContext* pDriverContext);
	virtual void Cleanup();

private:
	std::thread* m_pWatchdogThread;
};

CWatchdogDriver_optiforge g_watchdogDriverNull;


bool g_bExiting = false;

void WatchdogThreadFunction()
{
	while (!g_bExiting)
	{
#if defined( _WIN32 )
		// on windows send the event when the Y key is pressed.
		if ((0x01 & GetAsyncKeyState('Y')) != 0)
		{
			// Y key was pressed. 
			vr::VRWatchdogHost()->WatchdogWakeUp(vr::TrackedDeviceClass_HMD);
		}
		std::this_thread::sleep_for(std::chrono::microseconds(500));
#else
		// for the other platforms, just send one every five seconds
		std::this_thread::sleep_for(std::chrono::seconds(5));
		vr::VRWatchdogHost()->WatchdogWakeUp(vr::TrackedDeviceClass_HMD);
#endif
	}
}

EVRInitError CWatchdogDriver_optiforge::Init(vr::IVRDriverContext* pDriverContext)
{
	VR_INIT_WATCHDOG_DRIVER_CONTEXT(pDriverContext);
	InitDriverLog(vr::VRDriverLog());

	// Watchdog mode on Windows starts a thread that listens for the 'Y' key on the keyboard to 
	// be pressed. A real driver should wait for a system button event or something else from the 
	// the hardware that signals that the VR system should start up.
	g_bExiting = false;
	m_pWatchdogThread = new std::thread(WatchdogThreadFunction);
	if (!m_pWatchdogThread)
	{
		DriverLog("Unable to create watchdog thread\n");
		return VRInitError_Driver_Failed;
	}

	return VRInitError_None;
}


void CWatchdogDriver_optiforge::Cleanup()
{
	g_bExiting = true;
	if (m_pWatchdogThread)
	{
		m_pWatchdogThread->join();
		delete m_pWatchdogThread;
		m_pWatchdogThread = nullptr;
	}

	CleanupDriverLog();
}

class CoptiforgeDeviceDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent
{
public:
	CoptiforgeDeviceDriver()
	{
		memset(&serverAddr, 0, sizeof(serverAddr));

		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

		DriverLog("Using settings values\n");
		m_flIPD = vr::VRSettings()->GetFloat(k_pch_SteamVR_Section, k_pch_SteamVR_IPD_Float);

		char buf[1024];
		vr::VRSettings()->GetString(k_pch_optiforge_Section, k_pch_optiforge_SerialNumber_String, buf, sizeof(buf));
		m_sSerialNumber = buf;

		vr::VRSettings()->GetString(k_pch_optiforge_Section, k_pch_optiforge_ModelNumber_String, buf, sizeof(buf));
		m_sModelNumber = buf;

		m_nWindowX = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_WindowX_Int32);
		m_nWindowY = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_WindowY_Int32);
		m_nWindowWidth = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_WindowWidth_Int32);
		m_nWindowHeight = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_WindowHeight_Int32);
		m_nRenderWidth = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_RenderWidth_Int32);
		m_nRenderHeight = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_RenderHeight_Int32);
		m_flSecondsFromVsyncToPhotons = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_SecondsFromVsyncToPhotons_Float);
		m_flDisplayFrequency = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_DisplayFrequency_Float);
		PORT = vr::VRSettings()->GetInt32(k_pch_optiforge_Section, k_pch_optiforge_Port);

		vr::VRSettings()->GetString(k_pch_optiforge_Section, k_pch_optiforge_IP, buf, sizeof(buf));
		IP = buf;

		// Lighthouse tag mounted on the headset. Empty serial = use the first generic tracker found.
		vr::VRSettings()->GetString(k_pch_optiforge_Section, k_pch_optiforge_TrackerSerial_String, buf, sizeof(buf));
		m_sTagSerial = buf;

		// Transform from the tag to the head (centre between the eyes), expressed in the tag's own axes
		m_vecTagToHead[0] = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerOffsetX_Float);
		m_vecTagToHead[1] = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerOffsetY_Float);
		m_vecTagToHead[2] = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerOffsetZ_Float);
		float flYaw = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerYaw_Float);
		float flPitch = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerPitch_Float);
		float flRoll = vr::VRSettings()->GetFloat(k_pch_optiforge_Section, k_pch_optiforge_TrackerRoll_Float);
		m_qTagToHead = HmdQuaternion_Multiply(
			HmdQuaternion_Multiply(
				HmdQuaternion_FromAxisAngle(0, 1, 0, flYaw * k_flDegToRad),
				HmdQuaternion_FromAxisAngle(1, 0, 0, flPitch * k_flDegToRad)),
			HmdQuaternion_FromAxisAngle(0, 0, 1, flRoll * k_flDegToRad));

		DriverLog("driver_optiforge: Serial Number: %s\n", m_sSerialNumber.c_str());
		DriverLog("driver_optiforge: Model Number: %s\n", m_sModelNumber.c_str());
		DriverLog("driver_optiforge: Window: %d %d %d %d\n", m_nWindowX, m_nWindowY, m_nWindowWidth, m_nWindowHeight);
		DriverLog("driver_optiforge: Render Target: %d %d\n", m_nRenderWidth, m_nRenderHeight);
		DriverLog("driver_optiforge: Seconds from Vsync to Photons: %f\n", m_flSecondsFromVsyncToPhotons);
		DriverLog("driver_optiforge: Display Frequency: %f\n", m_flDisplayFrequency);
		DriverLog("driver_optiforge: IPD: %f\n", m_flIPD);
		DriverLog("driver_optiforge: Tracker serial: %s\n", m_sTagSerial.empty() ? "(first generic tracker)" : m_sTagSerial.c_str());
		DriverLog("driver_optiforge: Tracker offset: %f %f %f, rotation (yaw pitch roll): %f %f %f\n",
			m_vecTagToHead[0], m_vecTagToHead[1], m_vecTagToHead[2], flYaw, flPitch, flRoll);
	}

	virtual ~CoptiforgeDeviceDriver()
	{
		running_ = false;
	}

	virtual bool ComputeInverseDistortion(HmdVector2_t* pResult, EVREye eEye, uint32_t unChannel, float fU, float fV) override {
		if (!pResult || unChannel > 2 || eEye > vr::Eye_Right)
			return false;

		// Implement your actual inverse distortion model here
		// This is just an identity (pass-through) as a placeholder
		// You'd replace this with your distortion model logic
		pResult->v[0] = fU;
		pResult->v[1] = fV;
		return true;
	}


	virtual EVRInitError Activate(vr::TrackedDeviceIndex_t unObjectId) override
	{
		DriverLog("Activating device %d\n", unObjectId);
		running_ = true;

		m_unObjectId = unObjectId;
		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);


		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, Prop_ModelNumber_String, m_sModelNumber.c_str());
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, Prop_RenderModelName_String, m_sModelNumber.c_str());
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, Prop_UserIpdMeters_Float, m_flIPD);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, Prop_UserHeadToEyeDepthMeters_Float, 0.f);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, Prop_DisplayFrequency_Float, m_flDisplayFrequency);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, Prop_SecondsFromVsyncToPhotons_Float, m_flSecondsFromVsyncToPhotons);
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, Prop_DisplayDebugMode_Bool, true);

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		// Replaced with the lighthouse universe once the tag is found, see UpdateTag()
		vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, Prop_CurrentUniverseId_Uint64, m_ulUniverseId);

		// avoid "not fullscreen" warnings from vrmonitor
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, Prop_IsOnDesktop_Bool, false);

		bool bSetupIconUsingExternalResourceFile = false;
		if (!bSetupIconUsingExternalResourceFile)
		{
			// Setup properties directly in code.
			// Path values are of the form {drivername}\icons\some_icon_filename.png
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceOff_String, "{optiforge}/icons/headset_optiforge_status_off.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearching_String, "{optiforge}/icons/headset_optiforge_status_searching.gif");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{optiforge}/icons/headset_optiforge_status_searching_alert.gif");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReady_String, "{optiforge}/icons/headset_optiforge_status_ready.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{optiforge}/icons/headset_optiforge_status_ready_alert.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceNotReady_String, "{optiforge}/icons/headset_optiforge_status_error.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceStandby_String, "{optiforge}/icons/headset_optiforge_status_standby.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceAlertLow_String, "{optiforge}/icons/headset_optiforge_status_ready_low.png");
		}

		// RPi IMU connection, disabled while rotation comes from the lighthouse tag
		/*
#if defined( _WIN32 )
		wsaInit_ = WSAStartup(MAKEWORD(2, 2), &wsaData_);
		if (wsaInit_ != 0) {
			DriverLog("WSAStartup failed: %d", wsaInit_);
		}
		DriverLog("WSAStartup successful\n");
#endif

		if (!Connect()) {
			return vr::VRInitError_Driver_Failed;
		}

		// Start the UDP thread
		std::thread udpThread(&CoptiforgeDeviceDriver::TCPThread, this);
		udpThread.detach(); // Detach the thread to run independently
		*/

		return VRInitError_None;
	}

	bool Connect() {
		timeout = 0;
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(PORT);
		inet_pton(AF_INET, IP.c_str(), &serverAddr.sin_addr); // <-- Replace with your server's public IP

		// Create UDP socket
		sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (sock_ == INVALID_SOCKET) {
			DriverLog("Socket creation failed: %d", WSAGetLastError());
			WSACleanup();
			return false;
		}

		DriverLog("Socket created successfully\n");

		// Bind the socket
		if (connect(sock_, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
			DriverLog("Bind failed: %d", WSAGetLastError());
			closesocket(sock_);
			WSACleanup();
			return false;
		}

		DriverLog("Listening on port %d", PORT);
		return true;
	}

	virtual void Deactivate() override
	{
		running_ = false;
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		// RPi IMU connection, disabled while rotation comes from the lighthouse tag
		//closesocket(sock_);
		//WSACleanup();
	}

	virtual void EnterStandby() override
	{
		DriverLog("Entering standby");
	}

	void* GetComponent(const char* pchComponentNameAndVersion) override
	{
		if (!strcasecmp_portable(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version))
		{
			return (vr::IVRDisplayComponent*)this;
		}

		// override this to add a component to a driver
		return NULL;
	}

	virtual void PowerOff()
	{
	}

	/** debug request from a client */
	virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override
	{
		if (unResponseBufferSize >= 1)
			pchResponseBuffer[0] = 0;
	}

	virtual void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override
	{
		*pnX = m_nWindowX;
		*pnY = m_nWindowY;
		*pnWidth = m_nWindowWidth;
		*pnHeight = m_nWindowHeight;
	}

	virtual bool IsDisplayOnDesktop() override
	{
		return true;
	}

	virtual bool IsDisplayRealDisplay() override
	{
		return false;
	}

	virtual void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override
	{
		*pnWidth = m_nRenderWidth;
		*pnHeight = m_nRenderHeight;
	}

	virtual void GetEyeOutputViewport(EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override
	{
		*pnY = 0;
		*pnWidth = m_nWindowWidth / 2;
		*pnHeight = m_nWindowHeight;

		if (eEye == Eye_Left)
		{
			*pnX = 0;
		}
		else
		{
			*pnX = m_nWindowWidth / 2;
		}
	}

	virtual void GetProjectionRaw(EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) override
	{
		*pfLeft = -1.0;
		*pfRight = 1.0;
		*pfTop = -1.0;
		*pfBottom = 1.0;
	}

	virtual DistortionCoordinates_t ComputeDistortion(EVREye eEye, float fU, float fV) override
	{
		DistortionCoordinates_t coordinates;
		coordinates.rfBlue[0] = fU;
		coordinates.rfBlue[1] = fV;
		coordinates.rfGreen[0] = fU;
		coordinates.rfGreen[1] = fV;
		coordinates.rfRed[0] = fU;
		coordinates.rfRed[1] = fV;
		return coordinates;
	}

	virtual DriverPose_t GetPose() override
	{
		// Let's retrieve the Hmd pose to base our controller pose off.

	// First, initialize the struct that we'll be submitting to the runtime to tell it we've updated our pose.
		vr::DriverPose_t pose = { 0 };

		// These need to be set to be valid quaternions. The device won't appear otherwise.
		pose.qWorldFromDriverRotation.w = 1.f;

		// The "driver" pose is the tag's pose; this is the mounting transform from the tag to the head
		pose.qDriverFromHeadRotation = m_qTagToHead;
		pose.vecDriverFromHeadTranslation[0] = m_vecTagToHead[0];
		pose.vecDriverFromHeadTranslation[1] = m_vecTagToHead[1];
		pose.vecDriverFromHeadTranslation[2] = m_vecTagToHead[2];

		// Rotation from the RPi IMU, disabled while rotation comes from the lighthouse tag
		/*
		{
			std::lock_guard<std::mutex> lock(quatMutex); // Scoped lock for thread safety
			pose.qRotation.x = quat[0];
			pose.qRotation.y = quat[1];
			pose.qRotation.z = quat[2];
			pose.qRotation.w = quat[3];
		}
		*/

		if (m_bHasTagPose)
		{
			const HmdMatrix34_t& m = m_tagPose.mDeviceToAbsoluteTracking;
			pose.qRotation = HmdQuaternion_FromMatrix(m);
			pose.vecPosition[0] = m.m[0][3];
			pose.vecPosition[1] = m.m[1][3];
			pose.vecPosition[2] = m.m[2][3];

			// Only pass velocities while tracking, so a held pose doesn't get extrapolated away
			if (m_bTagTracking)
			{
				for (int i = 0; i < 3; i++)
				{
					pose.vecVelocity[i] = m_tagPose.vVelocity.v[i];
					pose.vecAngularVelocity[i] = m_tagPose.vAngularVelocity.v[i];
				}
			}

			pose.poseIsValid = true;

			// When the tag loses tracking we hold its last good pose, but let SteamVR know
			pose.result = m_bTagTracking ? vr::TrackingResult_Running_OK : vr::TrackingResult_Running_OutOfRange;
		}
		else
		{
			// Tag not seen yet
			pose.qRotation.w = 1.f;
			pose.poseIsValid = false;
			pose.result = vr::TrackingResult_Uninitialized;
		}

		pose.deviceIsConnected = true;

		// Position is tracked, so no neck model
		pose.shouldApplyHeadModel = false;

		return pose;
	}

	// Looks for the lighthouse tag among the tracked devices, returns true when found
	bool FindTag()
	{
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
		{
			if (i == m_unObjectId)
				continue;

			vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(i);
			if (container == vr::k_ulInvalidPropertyContainer)
				continue;

			vr::ETrackedPropertyError err;
			int32_t deviceClass = vr::VRProperties()->GetInt32Property(container, Prop_DeviceClass_Int32, &err);
			if (err != vr::TrackedProp_Success || deviceClass != vr::TrackedDeviceClass_GenericTracker)
				continue;

			std::string serial = vr::VRProperties()->GetStringProperty(container, Prop_SerialNumber_String, &err);
			if (err != vr::TrackedProp_Success || (!m_sTagSerial.empty() && serial != m_sTagSerial))
				continue;

			m_unTagId = i;
			DriverLog("driver_optiforge: Using tracker %s (device %d)\n", serial.c_str(), i);
			return true;
		}
		return false;
	}

	// Reads the tag's current pose and keeps our universe in sync with the lighthouse one
	void UpdateTag()
	{
		if (m_unTagId == vr::k_unTrackedDeviceIndexInvalid)
		{
			// Searching every frame is wasteful, once a second is plenty
			if (frame_number_ % 90 != 0 || !FindTag())
				return;
		}

		vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
		vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, poses, vr::k_unMaxTrackedDeviceCount);

		const vr::TrackedDevicePose_t& tag = poses[m_unTagId];
		m_bTagTracking = tag.bDeviceIsConnected && tag.bPoseIsValid && tag.eTrackingResult == vr::TrackingResult_Running_OK;
		if (m_bTagTracking)
		{
			m_tagPose = tag;
			m_bHasTagPose = true;
		}

		// Our pose is in the lighthouse tracking space, so we have to be in the same universe
		vr::ETrackedPropertyError err;
		vr::PropertyContainerHandle_t tagContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unTagId);
		uint64_t universe = vr::VRProperties()->GetUint64Property(tagContainer, Prop_CurrentUniverseId_Uint64, &err);
		if (err == vr::TrackedProp_Success && universe != 0 && universe != m_ulUniverseId)
		{
			m_ulUniverseId = universe;
			vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, Prop_CurrentUniverseId_Uint64, m_ulUniverseId);
			DriverLog("driver_optiforge: Switched to universe %llu\n", (unsigned long long)m_ulUniverseId);
		}
	}

	void TCPThread() {
		while (running_) {
			char buffer[BUFFER_SIZE];

			// Receive data from the socket
			int received = recv(sock_, buffer, BUFFER_SIZE, 0);

			if (received == SOCKET_ERROR) {
				DriverLog("Receive failed: %d", WSAGetLastError());
				continue;
			}
			
			if (received == BUFFER_SIZE) {
				std::lock_guard<std::mutex> lock(quatMutex);  // Ensure thread safety
				memcpy(quat, buffer, BUFFER_SIZE);
			}
			else {
				if (timeout > 10) {
					closesocket(sock_);
					Connect();
				}

				timeout++;

				DriverLog("Unexpected data size");
			}
		}
	}

	void RunFrame()
	{
		frame_number_++;

		// In a real driver, this should happen from some pose tracking thread.
		// The RunFrame interval is unspecified and can be very irregular if some other
		// driver blocks it for some periodic task.
		if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
		{
			UpdateTag();
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(), sizeof(DriverPose_t));
		}
	}

	std::string GetSerialNumber() const { return m_sSerialNumber; }

private:
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	std::string m_sSerialNumber;
	std::string m_sModelNumber;

	int32_t m_nWindowX;
	int32_t m_nWindowY;
	int32_t m_nWindowWidth;
	int32_t m_nWindowHeight;
	int32_t m_nRenderWidth;
	int32_t m_nRenderHeight;
	float m_flSecondsFromVsyncToPhotons;
	float m_flDisplayFrequency;
	float m_flIPD;

	bool running_ = false;
	int frame_number_ = 0;

	float quat[4] = { 0.0, 0.0, 0.0, 1.0 };
	std::mutex quatMutex;

	// Lighthouse tag used for the headset's pose
	std::string m_sTagSerial;
	vr::TrackedDeviceIndex_t m_unTagId = vr::k_unTrackedDeviceIndexInvalid;
	vr::TrackedDevicePose_t m_tagPose{};
	bool m_bHasTagPose = false;
	bool m_bTagTracking = false;
	HmdQuaternion_t m_qTagToHead = { 1, 0, 0, 0 };
	double m_vecTagToHead[3] = { 0, 0, 0 };
	uint64_t m_ulUniverseId = 2;

#if defined( _WIN32 )
	WSADATA wsaData_;
	int wsaInit_;
#endif
	SOCKET sock_;

	int PORT = 31000;
	std::string IP;

	sockaddr_in serverAddr{};

	int timeout = 0;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CoptiforgeControllerDriver : public vr::ITrackedDeviceServerDriver
{
public:
	CoptiforgeControllerDriver()
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

		m_sSerialNumber = "CTRL_1234";

		m_sModelNumber = "MyController";
	}

	virtual ~CoptiforgeControllerDriver()
	{
	}


	virtual EVRInitError Activate(vr::TrackedDeviceIndex_t unObjectId)
	{
		m_unObjectId = unObjectId;
		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);

		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, Prop_ModelNumber_String, m_sModelNumber.c_str());
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, Prop_RenderModelName_String, m_sModelNumber.c_str());

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, Prop_CurrentUniverseId_Uint64, 2);

		// avoid "not fullscreen" warnings from vrmonitor
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, Prop_IsOnDesktop_Bool, false);

		// our optiforge device isn't actually tracked, so set this property to avoid having the icon blink in the status window
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, Prop_NeverTracked_Bool, true);

		// even though we won't ever track we want to pretend to be the right hand so binding will work as expected
		vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, Prop_ControllerRoleHint_Int32, TrackedControllerRole_RightHand);

		// this file tells the UI what to show the user for binding this controller as well as what default bindings should
		// be for legacy or other apps
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, Prop_InputProfilePath_String, "{optiforge}/input/mycontroller_profile.json");

		// create all the input components
		vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/a/click", &m_compA);
		vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/b/click", &m_compB);
		vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/c/click", &m_compC);

		// create our haptic component
		vr::VRDriverInput()->CreateHapticComponent(m_ulPropertyContainer, "/output/haptic", &m_compHaptic);

		return VRInitError_None;
	}

	virtual void Deactivate()
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
	}

	virtual void EnterStandby()
	{
	}

	void* GetComponent(const char* pchComponentNameAndVersion)
	{
		// override this to add a component to a driver
		return NULL;
	}

	virtual void PowerOff()
	{
	}

	/** debug request from a client */
	virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
	{
		if (unResponseBufferSize >= 1)
			pchResponseBuffer[0] = 0;
	}

	virtual DriverPose_t GetPose()
	{
		DriverPose_t pose = { 0 };
		pose.poseIsValid = false;
		pose.result = TrackingResult_Calibrating_OutOfRange;
		pose.deviceIsConnected = true;

		pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
		pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);

		return pose;
	}


	void RunFrame()
	{
#if defined( _WIN32 )
		// Your driver would read whatever hardware state is associated with its input components and pass that
		// in to UpdateBooleanComponent. This could happen in RunFrame or on a thread of your own that's reading USB
		// state. There's no need to update input state unless it changes, but it doesn't do any harm to do so.

		vr::VRDriverInput()->UpdateBooleanComponent(m_compA, (0x8000 & GetAsyncKeyState('A')) != 0, 0);
		vr::VRDriverInput()->UpdateBooleanComponent(m_compB, (0x8000 & GetAsyncKeyState('B')) != 0, 0);
		vr::VRDriverInput()->UpdateBooleanComponent(m_compC, (0x8000 & GetAsyncKeyState('C')) != 0, 0);
#endif
	}

	void ProcessEvent(const vr::VREvent_t& vrEvent)
	{
		switch (vrEvent.eventType)
		{
		case vr::VREvent_Input_HapticVibration:
		{
			if (vrEvent.data.hapticVibration.componentHandle == m_compHaptic)
			{
				// This is where you would send a signal to your hardware to trigger actual haptic feedback
				DriverLog("BUZZ!\n");
			}
		}
		break;
		}
	}


	std::string GetSerialNumber() const { return m_sSerialNumber; }

private:
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	vr::VRInputComponentHandle_t m_compA;
	vr::VRInputComponentHandle_t m_compB;
	vr::VRInputComponentHandle_t m_compC;
	vr::VRInputComponentHandle_t m_compHaptic;

	std::string m_sSerialNumber;
	std::string m_sModelNumber;


};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CServerDriver_optiforge : public IServerTrackedDeviceProvider
{
public:
	virtual EVRInitError Init(vr::IVRDriverContext* pDriverContext);
	virtual void Cleanup();
	virtual const char* const* GetInterfaceVersions() { return vr::k_InterfaceVersions; }
	virtual void RunFrame();
	virtual bool ShouldBlockStandbyMode() { return false; }
	virtual void EnterStandby() {}
	virtual void LeaveStandby() {}

private:
	CoptiforgeDeviceDriver* m_pNullHmdLatest = nullptr;
	CoptiforgeControllerDriver* m_pController = nullptr;
};

CServerDriver_optiforge g_serverDriverNull;


EVRInitError CServerDriver_optiforge::Init(vr::IVRDriverContext* pDriverContext)
{
	VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
	InitDriverLog(vr::VRDriverLog());

	m_pNullHmdLatest = new CoptiforgeDeviceDriver();
	vr::VRServerDriverHost()->TrackedDeviceAdded(m_pNullHmdLatest->GetSerialNumber().c_str(), vr::TrackedDeviceClass_HMD, m_pNullHmdLatest);

	//m_pController = new CoptiforgeControllerDriver();
	//vr::VRServerDriverHost()->TrackedDeviceAdded(m_pController->GetSerialNumber().c_str(), vr::TrackedDeviceClass_Controller, m_pController);

	return VRInitError_None;
}

void CServerDriver_optiforge::Cleanup()
{
	CleanupDriverLog();
	delete m_pNullHmdLatest;
	m_pNullHmdLatest = NULL;
	delete m_pController;
	m_pController = NULL;
}


void CServerDriver_optiforge::RunFrame()
{

	if (m_pNullHmdLatest)
	{
		m_pNullHmdLatest->RunFrame();
	}
	/*
	if (m_pController)
	{
		m_pController->RunFrame();
	}

	vr::VREvent_t vrEvent;
	while (vr::VRServerDriverHost()->PollNextEvent(&vrEvent, sizeof(vrEvent)))
	{
		if (m_pController)
		{
			m_pController->ProcessEvent(vrEvent);
		}
	}
	*/
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
HMD_DLL_EXPORT void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
{
	if (0 == strcmp(IServerTrackedDeviceProvider_Version, pInterfaceName))
	{
		return &g_serverDriverNull;
	}
	if (0 == strcmp(IVRWatchdogProvider_Version, pInterfaceName))
	{
		return &g_watchdogDriverNull;
	}

	if (pReturnCode)
		*pReturnCode = VRInitError_Init_InterfaceNotFound;

	return NULL;
}