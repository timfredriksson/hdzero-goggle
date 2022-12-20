#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/vfs.h>
#include <dirent.h>

#include "defines.h"
#include "thread.h"
#include "osd.h"
#include "input_device.h"
#include "ht.h"
#include "common.h"
#include "../driver/porting.h"
#include "../driver/mcp3021.h"
#include "../driver/nct75.h"
#include "../driver/hardware.h"
#include "../driver/oled.h"
#include "../driver/dm5680.h"
#include "../driver/it66021.h"
#include "../driver/it66021.h"
#include "../page/page_fans.h"
#include "../page/page_version.h"
#include "msp_displayport.h"


///////////////////////////////////////////////////////////////////////////////
// SD card exist
static void detect_sdcard(void)
{
	static bool sdcard_enable_last = false;

	g_sdcard_enable = false;

	DIR *dir = opendir("/mnt/extsd/");
	if (dir != NULL) {
		struct dirent *d;
		while ((d = readdir(dir)) != NULL) {
			if (d->d_name[0] != '.') {
				g_sdcard_enable = true;
				break;
			}
		}
		closedir(dir);
	}

	if((g_sdcard_enable && !sdcard_enable_last) || g_sdcard_det_req) {
		struct statfs info;
		if(statfs( "/mnt/extsd", &info ) != -1) 
			g_sdcard_size = (info.f_bsize * info.f_bavail)>>20; //in MB
		else
			g_sdcard_size = 0;
		g_sdcard_det_req = 0;
	}
	sdcard_enable_last = g_sdcard_enable;
}

static void *thread_imu(void *ptr)
{
	int cnt = 0;
	for(;;)
	{
		get_imu_data(true);
		calc_ht();
		if(cnt++ == 9) {
			cnt = 0;
			seconds++;
		}
		usleep(100000); //0.1s
	}
	return NULL;
}

static void *thread_dialpad(void *ptr)
{
	for(;;)
	{
		input_device_loop();
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
// Signal loss|accquire processing
#define SIGNAL_LOSS_DURATION_THR  20 //25=4 seconds, 
#define SIGNAL_ACCQ_DURATION_THR  10 
static void check_hdzero_signal(int vtmg_change)  //fix me. ntant, Need to consider source is HDMI or analog
{
	static uint8_t cnt = 0;
	uint8_t is_valid;

	//HDZero digital 
	if(g_source_info.source == 0) {
		DM5680_req_rssi();
		DM5680_req_vldflg();
		tune_channel_timer();
	}

	if(g_setting.record.mode_manual || !g_sdcard_enable || (g_menu_op != OPLEVEL_VIDEO)) return;

	//exit if HDMI in
	if(g_source_info.source == 1) return; 

	//Analog
	if(g_source_info.source >= 2) {
		if(vtmg_change && is_recording) {
			LOGI("AV VTMG change");
			rbtn_click(1,1);
			rbtn_click(1,2);
		}
		return; 
	}

	is_valid = rx_status[0].rx_valid || rx_status[1].rx_valid;

	if(vtmg_change) {
		LOGI("VTMG change");
		rbtn_click(1,1);
		cnt = 0;
	}

	if(is_recording) { //in-recording
		if(!is_valid) {
			cnt++;
			if(cnt >= SIGNAL_LOSS_DURATION_THR) {
				cnt = 0;
				LOGI("Signal lost");
				rbtn_click(1,1);
			}
		}
		else 
			cnt = 0;
	}
	else { //not in-recording
		if(is_valid) {
			cnt++;
			if(cnt >= SIGNAL_ACCQ_DURATION_THR) {
				cnt = 0;
				LOGI("Signal accquired");
				rbtn_click(1,2);
			}
		}
		else 
			cnt = 0;
	}
}

static void *thread_peripheral(void *ptr)
{
	int record_vtmg_change = 0;
	int j=0,k=0;

	for(;;)
	{
		if(j>50)
		{
			j=0;
			
			fans_auto_ctrl();
			detect_sdcard();
			if(k++ == 4) {
				k = 0;
				g_battery.voltage = mcp_read_vatage();
				g_temperature.top = nct_read_temperature(NCT_TOP);
				g_temperature.left = nct_read_temperature(NCT_LEFT);
				g_temperature.right= nct_read_temperature(NCT_RIGHT);
			}
            // detect HDZERO
			record_vtmg_change = HDZERO_detect();

            // detect AV_in/Moudle_bay
			record_vtmg_change |= AV_in_detect();
			g_source_info.av_in_status = g_hw_stat.av_valid[0];
			g_source_info.av_bay_status = g_hw_stat.av_valid[1];
            
			// detect HDMI in
			HDMI_in_detect();
			g_source_info.hdmi_in_status = g_hw_stat.hdmiin_valid;

			g_latency_locked = (bool)Get_VideoLatancy_status();
			check_hdzero_signal(record_vtmg_change);
			record_vtmg_change = 0;
		}
		j++;
		usleep(2000); 
	}
	return NULL;
}



//////////////////////////////////////////////////////////////////////
//local
static threads_obj_t threads_obj;

static void threads_instance(threads_obj_t *obj)
{
	obj->instance[0] =  thread_dialpad;
	obj->instance[1] =  thread_peripheral;
	obj->instance[2] =  thread_version;
	obj->instance[3] =  thread_osd;
	obj->instance[4] =  thread_imu;
}

int create_threads()
{
	int ret = 0;

    threads_obj_t *obj = &threads_obj; 
	threads_instance(obj);
	for(int i=0; i<THREAD_COUNT; i++)
	{
		ret = pthread_create(&obj->pid[i],
			   	NULL,
				obj->instance[i],
				NULL);

		if(ret != 0)
			goto thread_create_err;
		
	}
	return 0;

thread_create_err:
	return -1;
}
