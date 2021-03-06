/*
*定义了M2M与服务器的功能
*
*********************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "eat_modem.h"
#include "eat_interface.h"
#include "eat_uart.h"
#include "OBDapi.h"
#include "definedata.h"
#include "UartTask.h"
#include "ATapi.h"
#include "SVRapi.h"
#include "db.h"
#include "BigMem.h"

#define SER_APP_BUF_MAX 512
#define SER_FRAME_FMT1 0xa5
#define SER_FRAME_FMT2 0xa5
static u8 SEV_APP_BUF[SER_APP_BUF_MAX];
/*M2M中使用的所有时间为UTC时间
*/
static u32 G_system_time = 0;//系统时间戳
u8 LGPS_SYSTEM_TIME_SYNCHRON = 0;//GPS与系统时钟同步标志 每次系统启动首次获取到有效的GPS时钟时需要将系统时钟同步到GPS时钟
/*使用当前时间设置系统时钟
***************************************/
u8 G_system_time_SetEx(void){
	EatRtc_st rtc = {0};
	u32 u32t1;
	eat_bool result ;
	//3b1265a0
	rtc.year = (G_system_time >> 26) & 0x003f;//0011 10 14
	if(rtc.year < 14)return 1;//异常
	rtc.mon = (G_system_time >> 22) & 0x000f; //1100 12
	if(rtc.mon > 12)return 2;
	rtc.day = (G_system_time >> 17) & 0x001f;//0 1001 9
	if(rtc.day > 31)return 3;
	u32t1 = G_system_time & 0x1ffff;//65a0 26016
	rtc.hour = u32t1 / 3600; //15
	rtc.min = (u32t1 % 3600) / 60;
	rtc.sec = (u32t1 % 3600) % 60;
	result = eat_set_rtc(&rtc);
	
	Lstime_sys_update(G_system_time);
	if(result){
		   user_infor("e:G_system_time_set:system-time set ok");
		   LGPS_SYSTEM_TIME_SYNCHRON = 0x55;
		   return 0;
	}
	else{
	     user_debug("i:G_system_time_set:system-time set error");
	     return 1;   
	}
}
u8 G_system_time_SetExEx(unsigned int time){
	EatRtc_st rtc = {0};
	u32 u32t1;
	eat_bool result ;
	//3b1265a0
	rtc.year = (time >> 26) & 0x003f;//0011 10 14
	if(rtc.year < 14)return 1;//异常
	rtc.mon = (time >> 22) & 0x000f; //1100 12
	if(rtc.mon > 12)return 2;
	rtc.day = (time >> 17) & 0x001f;//0 1001 9
	if(rtc.day > 31)return 3;
	u32t1 = time & 0x1ffff;//65a0 26016
	rtc.hour = u32t1 / 3600; //15
	rtc.min = (u32t1 % 3600) / 60;
	rtc.sec = (u32t1 % 3600) % 60;
	result = eat_set_rtc(&rtc);
	if(result){
		   user_infor("e:G_system_time_set:system-time set ok");
		   return 0;
	}
	else{
	     user_debug("i:G_system_time_set:system-time set error");
	     return 1;   
	}
}
/*通过系统时间设置当前时间
**********************************************/
u8 G_system_time_get(void)
{
 	EatRtc_st rtc = {0};
 	u8 data;
 	eat_bool result ;
 	u32 times;
 
 	result = eat_get_rtc(&rtc);
 //user_debug("G_system_time_get[%d]",result);
 	if(result)
	 {//3
	  	data = rtc.year & 0x03f;
	  	if(data < 14)
	  	{
	   		G_system_time = Lstime_get();
	    	 	if(0 == G_system_time)
			{
	        		return 1;
	     		}
	     		return 0;
	  	}
	   	G_system_time = (data << 26);
	   	data = rtc.mon & 0x0f;
	   	if(data > 12)
		{
	    		G_system_time = Lstime_get();
	     		if(0 == G_system_time)
			{
	        		return 2;
	     		}
	     		return 0;
	   	}
	   	G_system_time |= (data << 22);
	   	data = rtc.day & 0x1f;
	   	if(data > 31)
		{
	    		G_system_time = Lstime_get();
	     		if(0 == G_system_time)
			{
	        		return 3;
	     		}
	     		return 0;
	   	}
	   	G_system_time |= (data << 17);
	   	times = rtc.sec & 0x00ff;
	   	times += (rtc.min & 0x00ff) * 60;
	   	times += (rtc.hour & 0x00ff) * 60 * 60;
	   	G_system_time += times; 
	   	Lstime_sys_update(G_system_time);
	   //user_debug("G_system_time_get[%d]", G_system_time);
	   	return 0;
	 }
 	else
	 {
	     	G_system_time = Lstime_get();
	     	if(0 == G_system_time)
	     	{
	        	user_debug("i:G_system_time_get:system-time get error");
	        	return 1;
	     	}
	     	return 0;
	 }
}

u32 G_system_time_getEx(void){
	if(0 == G_system_time_get()){
		//user_debug("systime_TTTT=%d", G_system_time);
		return G_system_time;
	}
	return 0;
}
/*时钟同步机制
*1、获取系统时钟 并判断系统时钟是否正常 y >= 14
*2、测试当前时间是否有效 是否已经从GPS提取到有效时钟
*3、如果系统时钟无效但当前时钟有效则采用当前时钟设置系统时钟
*4、如果系统时钟无效且当前时钟也无效则向服务器申请系统时钟
*****************************************************/
u8 G_system_time_synchron(void){
	EatRtc_st rtc = {0};
	eat_bool result ;
	u8 yy;
	
	user_infor("e:G_system_time_synchron..");
	yy = (G_system_time >> 26) & 0x003f;
	result = eat_get_rtc(&rtc);
	if( result )
	{
		if(rtc.year >= 14)
		{//系统时钟有效
			if(yy == rtc.year)
			{//系统时钟正确
				user_infor("e:here:%d",rtc.year);
				return 0;
			}
			else
			{//当前时钟无效
			    if(yy >= 14)
			    {//系统时钟异常 采用当前时钟同步
			    		user_infor("e:here 1");
			    		return G_system_time_SetEx();
			    }
			    else
			    {//当前时钟异常 通过系统时钟同步
			      		user_infor("e:here 2");
			      		return G_system_time_get();
			    }
		  	}
		}
		else
		{//系统时钟无效
		   	if(yy >= 14)
			{//当前时钟有效
		   	   	user_infor("e:here 3:%d",yy);
		   	   	if(G_system_time_SetEx() != 0)SVR_system_synchron();
		   	   	return G_system_time_SetEx();
		   	}
		   	else
			{//当前时钟无效 需要从服务器获取时钟
		       	if(0 == SVR_system_synchron())
				{
		       	   	user_infor("e:here 4");
		       	   	return G_system_time_SetEx();
		       	}
		   	}
	  	}
	}
	user_debug("i:G_system_time_synchron error");
	return 1;
}
/*
*time:hhmmss
*ymd:ddmmyy
*需要将这两个时间转换成一个int数据
*bit31-bit26:yy
*bit25-bit22:mm
*bit21-bit17:dd
*bit16-bit0:time
*******************************************/
void G_system_time_Set(u32 ymd, u32 time)
{
	u32 times;
	
	if(0 == ymd)return;
	if(0x55 == LGPS_SYSTEM_TIME_SYNCHRON && ((G_system_time >> 26) & 0x003f) >= 14){//
		return;
	}
	
	times = ydmhms2u32(ymd, time);
	if(times != 0)
	{
		G_system_time = times;
	  	G_system_time_SetEx();
	}
}

/*
*完成：数据封包
*      数据发送
*输入：data = [命令字 + 数据]
       datalen = data的字节个数
******************************************************/
u8 SVR_FrameSend(u8 *data, u32 datalen)
{
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen)return 1;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	G_system_time_get();
	//2015/12/29 11:18 fangcuisong
	if(0 == G_system_time && *(data) != 0x03 && *(data) != 0x8d && *(data) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 0) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
	cs = SEV_APP_BUF[dataindex - 1];
	SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
	cs += SEV_APP_BUF[dataindex - 1];
	for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++)
	{
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
		cs += *(data + dataindex1);
	}
	SEV_APP_BUF[dataindex ++] = cs;
	u8result = 0;
	if(m2m_status() < 8)
	{//还未完成时间同步
		if(*(data) == 0x03 || *(data) == 0x8d || *(data) == 0x09 || (0x14 == *(data) && 0x20 == *(data + 1)))return AT_CIPSEND(SEV_APP_BUF, dataindex);      
		else if(G_system_time > 1)
		{
			if( *(data) != 0x84)               //add by lilei-2016-0826  防止0x84 升级指令存在file。下次刚启动就发升级
			{
				bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有�
			}
		}
	}
	else
	{
	  	u8result = AT_CIPSEND(SEV_APP_BUF, dataindex);
	  	if(u8result != 0)
		{
	 	  	if(G_system_time > 1 &&*(data) != 0x84)
			{
	 	  		u8result = 0;
	 	  		bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有效
	 	  	}
	  	}
  	}
	return u8result;
}


/*
*完成：数据封包
*      数据发送
*输入：data = [命令字 + 数据]
       datalen = data的字节个数
******************************************************/
u8 SVR_FrameSend_test(u8 *data, u32 datalen){
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen)return 1;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	G_system_time_get();
	if(0 == G_system_time && *(data) != 0x03 && *(data) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 0) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
	cs = SEV_APP_BUF[dataindex - 1];
	SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
	cs += SEV_APP_BUF[dataindex - 1];
	for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++){
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
		cs += *(data + dataindex1);
	}
	SEV_APP_BUF[dataindex ++] = cs;
	u8result = 0;
	return AT_CIPSEND_test(SEV_APP_BUF, dataindex);
}


/*数据透传到服务器 只增加时间戳
*完成：数据封包
*      数据发送
*输入：data = [0xa5 0xa5 + len1 + len2 + 命令字 + 数据]
       datalen = data的字节个数
******************************************************/
u8 SVR_FrameSendEx(u8 *data, u32 datalen, u32 time)
{
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen)return 1;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	if(0 == time  && *(data + 4) != 0x03 && *(data + 4) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
	SEV_APP_BUF[dataindex ++] = (time >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 0) & 0x00ff;
	for(dataindex1 = 2; dataindex1 < datalen; dataindex1 ++)
	{
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
	}
	//debug_hex("FrameSend:", SEV_APP_BUF, dataindex);
	if(m2m_status() < 8)
	{//还未完成时间同步
		if(*(data + 4) == 0x03 || *(data + 4) == 0x09 || (0x14 == *(data + 4) && 0x20 == *(data + 5)))return AT_CIPSEND(SEV_APP_BUF, dataindex);
		else if(G_system_time > 1)bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有效
	}
	else
	{
	  	u8result = AT_CIPSEND(SEV_APP_BUF, dataindex);
	  	if(u8result != 0)
		{
	 	  	if(G_system_time > 1)bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有效
	  	}
  	}
	return 0;
}


/*数据透传到服务器 时间戳需要外部接口设定
*完成：数据封包
*      数据发送
*输入：data = [命令字 + 数据]
       datalen = data的字节个数
******************************************************/
u8 SVR_FrameSendExEx(u8 *data, u32 datalen, u32 time){
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen)return 1;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	if(0 == time && *(data) != 0x03 && *(data) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
	SEV_APP_BUF[dataindex ++] = (time >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (time >> 0) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
	cs = SEV_APP_BUF[dataindex - 1];
	SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
	cs += SEV_APP_BUF[dataindex - 1];
	for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++){
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
		cs += *(data + dataindex1);
	}
	SEV_APP_BUF[dataindex ++] = cs;
	//debug_hex("FrameSend:", SEV_APP_BUF, dataindex);
	if(m2m_status() < 8){//还未完成时间同步
		if(*(data) == 0x03 || *(data) == 0x09 || (0x14 == *(data) && 0x20 == *(data + 1)))return AT_CIPSEND(SEV_APP_BUF, dataindex);
		else if(G_system_time > 1)bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有效
	}
	else{
	  u8result = AT_CIPSEND(SEV_APP_BUF, dataindex);
	  if(u8result != 0){
	 	  if(G_system_time > 1)bigmem_obdgps_in(SEV_APP_BUF, dataindex);//时间必须有效
	  }
  }
	return 0;
}


/*数据不发送 直接保存到存储器中
*/
u8 SVR_FrameSend_Mem(u8 *data, u32 datalen, u32 time){
 u16 dataindex,dataindex1;
 u8 cs,u8result;
 
 if(NULL == data || 0 == datalen)return 1;
 dataindex = 0;
 SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
 SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
 G_system_time_get();
 if(0 == time && *(data) != 0x03 && *(data) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
 SEV_APP_BUF[dataindex ++] = (time >> 24) & 0x00ff;
 SEV_APP_BUF[dataindex ++] = (time >> 16) & 0x00ff;
 SEV_APP_BUF[dataindex ++] = (time >> 8) & 0x00ff;
 SEV_APP_BUF[dataindex ++] = (time >> 0) & 0x00ff;
 SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
 cs = SEV_APP_BUF[dataindex - 1];
 SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
 cs += SEV_APP_BUF[dataindex - 1];
 for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++){
  SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
  cs += *(data + dataindex1);
 }
 SEV_APP_BUF[dataindex ++] = cs;
  bigmem_obdgps_in(SEV_APP_BUF, dataindex);//必须有效
 return 0;
}

/**
*读取服务器返回的指令
*该接口只处理OBD的通讯协议，其他协议数据直接抛弃
*返回：数据长度
*************************************/
u8 SVR_FrameRead(u8 *cmd1, u8 *cmd2, u8 **dataread, u32 *datalen)
{
	u16 len,len1,dataindex;
	u8 *datatemp;
	u8 cmdt1,cmdt2,cs;
	
	datatemp = NULL;
	len = MDM_DataToApp(&datatemp);
	if(len < 7 || len > 512)
	{
		//user_debug("FrameRead error:%d", len);
		return 1;
	}
	if(NULL == datatemp)return 1;
	//debug_hex("FrameRad:", datatemp, len);
	//服务器返回的数据可能与其他数据混在一起
	for(dataindex = 0; dataindex < len; dataindex ++)
	{
		if(SER_FRAME_FMT1 == *(datatemp + dataindex) && SER_FRAME_FMT2 == *(datatemp + dataindex + 1))
		{
			app_svrlink(0);
			app_keep_step_set(0);//短时间不需要保持
			//user_debug("SVR_FrameRead:%02x-%02x", *(datatemp + dataindex + 4),*(datatemp + dataindex +5));
			break;
		}
	}
	len = len - dataindex;
	if(len < 7)
	{
		user_debug("i:FrameRead error:[%d-%d]", len, dataindex);
		return 2;
	}
	datatemp = datatemp + dataindex;
	dataindex = 2;
	len1 = *(datatemp + dataindex ++);
	cs = len1 & 0x00ff;
	len1 = (len1 << 8) + *(datatemp + dataindex ++);
	cs = cs + (len1 & 0x00ff);
	if(len1 != len -5 || len1 < 2){
		user_debug("i:FreadRead-lenerror[%d-%d]", len1, len);
		SVR_back7f(0, 0x04);
		return 3;
	}
	*cmd1 = *(datatemp + dataindex ++);
	cs = cs + (*cmd1);
	*cmd2 = *(datatemp + dataindex ++);
	cs = cs + (*cmd2);
	for(dataindex = 0; dataindex < len1 - 2; dataindex ++)
	{
		SEV_APP_BUF[dataindex] = *(datatemp + 6 + dataindex);
		cs = cs + SEV_APP_BUF[dataindex];
	}
	if(cs != *(datatemp + len -1))
	{//校验错误
		user_debug("i:FrameRead-CS-error:[%02x-%02x-%d]", cs,*(datatemp + len -1), len); 
		SVR_back7f(*cmd1, 0x02);
		return 4;
	}
	*datalen = dataindex;
	*dataread = SEV_APP_BUF;
	return 0;
}

void SVR_SMSdeal(u8 *data){
	u16 datalen, dataindex;
	u8 *obdver,*m2mver,*tellsms;
	u8 cmds[128],cmdindex;
	u8 tell[16],u8t1;
	u32 lat, lon, speed, angle, u32t1;
	
	if(NULL == data)return;
	datalen = strlen((s8 *)data);
	memset((s8 *)tell, 0, 12);
	for(dataindex = 0; dataindex < datalen; dataindex ++){
		if(0x22 == *(data + dataindex)){
			dataindex +=1;
			if('+' == *(data + dataindex))dataindex += 3;//跳过+86
			cmdindex = 0;
			for(; dataindex < datalen; dataindex ++){
				if(0x22 == *(data + dataindex))break;
				tell[cmdindex] = *(data + dataindex);
				if(cmdindex < 11)cmdindex ++;
			}
			break;
		}
	}
	for(dataindex = datalen; dataindex > 0; dataindex --){
		if(0x22 == *(data + dataindex)){
			dataindex ++;
			break;
		}
	}
	cmdindex = 0;
	memset((s8 *)cmds, 0, 42);
	for(; dataindex < datalen; dataindex ++){
		if(0x0d == *(data + dataindex) || 0x0a == *(data + dataindex))continue;
		cmds[cmdindex] = *(data + dataindex);
		if(cmdindex < 128)cmdindex ++;
	}
	user_debug("i:SVR_SMSdeal[%s-%s]", tell, cmds);
	if('1' == cmds[0] && '4' == cmds[1]){//需要返回GPS坐标到系统
		lat = 0;
		lon = 0;
		speed = 0;
		angle = 0;
		gps_data_get(&lat, &lon, &speed, &angle);
		sprintf((s8 *)cmds,"1401%08d,%08d,%03d,%08d",lat,lon,speed,angle);
		user_debug("i:SVR_SMSdeal-1[%s-%s]", tell, cmds);
		AT_SMSENDex(tell, cmds);
	}
	else if('A' == cmds[0] && 'T' == cmds[1] && '+' == cmds[2]){//AT指令
		if('A' == cmds[3] && 'P' == cmds[4] && 'N' == cmds[5]){
			db_svr_apnset(&cmds[7]);
			db_svr_save();//同步到数据库
		}
		else if('T' == cmds[3] && 'E' == cmds[4] && 'L' == cmds[5]){
			db_svr_ttellset(&cmds[7]);
			db_svr_save();//同步到数据库
		}
		else if('I' == cmds[3] && 'P' == cmds[4] && 'P' == cmds[5]){
			db_svr_addr1set(&cmds[7]);
			db_svr_save();//同步到数据库
		}
		else if('P' == cmds[3] && 'R' == cmds[4] && 'T' == cmds[5]){
			u32t1 = 0;
			for(u8t1 = 7; u8t1 < cmdindex; u8t1 ++){//2015/6/3 10:44 fangcuisong dataindex--> cmdindex
				if(0 == cmds[u8t1])break;
				u32t1 = u32t1 * 10 + (cmds[u8t1] - '0');
			}
			db_svr_port1set(u32t1);
			db_svr_save();//同步到数据库
		}
		else if('M' == cmds[3] && 'M' == cmds[4] && 'C' == cmds[5]){
			if('0' == cmds[7])db_svr_mmcset(0);
			else db_svr_mmcset(1);
			db_svr_save();//同步到数据库
		}
		else if('A' == cmds[3] && 'T' == cmds[4] && 'S' == cmds[5]){//AT指令发送到通讯模块 2015/7/9 15:41 FangCuisong
			AT_SendEx(&cmds[7]);
		}
		else if('V' == cmds[3] && 'E' == cmds[4] && 'R' == cmds[5]){//短信获取当前软件版本
			obdver = db_update_fileget(0x0c);
			m2mver = db_update_fileget(0x10);
			sprintf(cmds, "MCU=%s,M2M=%s", obdver, m2mver);
			tellsms = db_svr_ttellget();
		  if(tellsms != NULL && strlen((char *)tellsms) > 3){
			    AT_SMSENDex(tellsms,cmds);
	        eat_sleep(5000);//需要等待5S 否则短信可能无法发出
		  }
		}
	}
	else if('1' == cmds[0] && '9' == cmds[1]){//短信设置硬件防盗
		hw_th_unable_set(1);
	}
}

/*数据封包
*/
u32 SVR_DataMessage(u8 *data, u32 datalen){
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen)return 1;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	G_system_time_get();
	if(0 == G_system_time && *(data) != 0x03 && *(data) != 0x09 && (*(data) != 0x14 || *(data + 1) != 0x20))return 0;//如果时间为0 数据直接丢弃 该数据无效
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (G_system_time >> 0) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
	cs = SEV_APP_BUF[dataindex - 1];
	SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
	cs += SEV_APP_BUF[dataindex - 1];
	for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++){
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
		cs += *(data + dataindex1);
	}
	SEV_APP_BUF[dataindex ++] = cs;
	memcpy((s8 *)data, (s8 *)SEV_APP_BUF, dataindex);
	return dataindex;
}
/*数据封包 带时间戳
*/
u32 SVR_DataMessageEx(u8 *data, u32 datalen, u32 timeflag){
	u16 dataindex,dataindex1;
	u8 cs,u8result;
	
	if(NULL == data || 0 == datalen || 0 == timeflag)return 0;
	dataindex = 0;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT1;
	SEV_APP_BUF[dataindex ++] = SER_FRAME_FMT2;
	
	SEV_APP_BUF[dataindex ++] = (timeflag >> 24) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (timeflag >> 16) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (timeflag >> 8) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (timeflag >> 0) & 0x00ff;
	SEV_APP_BUF[dataindex ++] = (datalen >> 8) & 0x00ff;
	cs = SEV_APP_BUF[dataindex - 1];
	SEV_APP_BUF[dataindex ++] = (datalen >> 0) & 0x00ff;
	cs += SEV_APP_BUF[dataindex - 1];
	for(dataindex1 = 0; dataindex1 < datalen; dataindex1 ++){
		SEV_APP_BUF[dataindex ++] = *(data + dataindex1);
		cs += *(data + dataindex1);
	}
	SEV_APP_BUF[dataindex ++] = cs;
	memcpy((s8 *)data, (s8 *)SEV_APP_BUF, dataindex);
	return dataindex;
}


/*该接口由GSM业务处理线程调用
*接口判断当前数据是否为OBD数据
*返回:0x7f = 非OBD数据
      0 = OBD数据且已经被处理
      其他 = 数据错误
*****************************************/
u8 SVR_FameDeal(u8 *framedata, u32 datalen){
	u16 len1,dataindex;
	u32 len;
	u8 *datatemp;
	u8 cmdt1,cmdt2,cs;
	
	
	if(NULL == framedata || datalen < 7)return 1;
	datatemp = NULL;
	datatemp = framedata;
	len = datalen;
	if(len < 7 || len > 512){
		user_debug("i:FrameRead error:%d", len);
		return 0x7f;
	}
	if(NULL == datatemp)return 0x7f;
	
	//debug_hex("FrameRad:", datatemp, len);
	//服务器返回的数据可能与其他数据混在一起
	for(dataindex = 0; dataindex < len; dataindex ++){
		if(SER_FRAME_FMT1 == *(datatemp + dataindex) && SER_FRAME_FMT2 == *(datatemp + dataindex + 1)){
			//user_debug("FrameDeal:%02x-%02x", *(datatemp + dataindex + 4),*(datatemp + dataindex +5));
			app_svrlink(0);
			app_keep_step_set(0);
			break;
		}
	}
	
	len = len - dataindex;
	if(len < 7){//不是OBD数据
		//user_debug("FrameRead error:%d %s", len,framedata);
		return 0x7f;
	}
	datatemp = datatemp + dataindex;
	dataindex = 2;
	len1 = *(datatemp + dataindex ++);
	cs = len1 & 0x00ff;
	len1 = (len1 << 8) + *(datatemp + dataindex ++);
	cs = cs + (len1 & 0x00ff);
	if(len1 != len -5 || len1 < 2){
		user_debug("i:FreadRead-lenerror[%d-%d]", len1, len);
		SVR_back7f(0, 0x04);
		return 3;
	}
	cmdt1 = *(datatemp + dataindex ++);
	cs = cs + cmdt1;
	cmdt2 = *(datatemp + dataindex ++);
	cs = cs + cmdt2;
	memcpy(SEV_APP_BUF, datatemp, 6);
	for(dataindex = 0; dataindex < len1 - 2; dataindex ++){
		SEV_APP_BUF[6 + dataindex] = *(datatemp + 6 + dataindex);
		cs = cs + SEV_APP_BUF[6 + dataindex];
	}
	if((0x84 == cmdt1));//不 校验 由后面接口完成校验
	else{
	    if(cs != *(datatemp + len -1)){//校验错误
	    	user_debug("i:FrameRead-CS-error:[%02x-%02x-%d]", cs,*(datatemp + len -1), len); 
	    	SVR_back7f(cmdt1, 0x02);
	    	return 4;
	    }
	}
	SEV_APP_BUF[6 + dataindex] = *(datatemp + len -1);
	dataindex ++;
	//user_debug("FrameDeal:%02x-%02x", cmdt1,cmdt2);
	SVR_Cmd(cmdt1, cmdt2, SEV_APP_BUF, 6 + dataindex);
	return 0;
}
/*否定应答
***************************************/
u8 SVR_back7f(u8 cmdsub, u8 status){
	u8 datasend[3];
	
	return 0;
	datasend[0] = 0x7f;
	datasend[1] = cmdsub;
	datasend[2] = status;
	return SVR_FrameSend(datasend, 3);
}


/*心跳包
******************/
u8 SVR_heard(void){
	u8 data[24];
	u8 u8result;
	int datalen;
	char *imei;
	
	memset(data, 0, 24);
	app_keep_step_set(0);
	data[0] = 0x01;
	data[1] = 0x00;
	
	imei = m2m_imeiget();
	if(imei){
		strcpy((char *)&data[2], imei);
		datalen = strlen(imei) + 3;
	}
	else datalen = 3;
	u8result = SVR_FrameSend(data, datalen);
	return u8result;
}

/*
*注册
* tel(11byts) + id(16byts)
**************************/
u8 SVR_logon(void)
{
	u8 data[32];
	u8 u8result;
	u8 cmd1,cmd2,*dataread;
	u8 *productserid;
	u32 datalen;
	u32 time;
	
	memset(data, 0, 32);
	data[0] = 0x03;
	data[1] = 0x01;
	memcpy((s8 *)&data[2], "12345678901", 12);
	productserid = m2m_imeiget();//用 IMEI作为设备ID使用
	memcpy((s8 *)&data[14], productserid/*"1405250028SI"*/,/*13*/strlen((s8 *)productserid) + 1);
	u8result = SVR_FrameSend(data, /*27*/14 + strlen((s8 *)productserid) + 1);
	if(u8result != 0)
	{
		user_debug("i:SVR_logon send error:%d", u8result);
		return u8result;
	}
	time = 0;
	while(1)
	{
		if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen))
		{
			if(0x03 == cmd1 && 0x01 == cmd2 && 0x01 == datalen)
			{
				if(0x00 == *dataread)return 0;
				else
				{
				 	user_debug("i:SVR_logon rec error:%d", *dataread);
				 	return 0x7f;
				}
			}
		}
		time ++;
		if(time > 2000)
		{
			user_debug("i:SVR_logon rec overtime");
			return 1;
		}
		eat_sleep(5);
	}
	return 2;
}


u8 SVR_logon_test(void){
	u8 data[32];
	u8 u8result;
	u8 cmd1,cmd2,*dataread;
	u8 *productserid;
	u32 datalen;
	u32 time;
	
	memset(data, 0, 32);
	data[0] = 0x03;
	data[1] = 0x01;
	memcpy((s8 *)&data[2], "12345678901", 12);
	productserid = m2m_imeiget();//用 IMEI作为设备ID使用
	memcpy((s8 *)&data[14], productserid/*"1405250028SI"*/,/*13*/strlen((s8 *)productserid) + 1);
	u8result = SVR_FrameSend_test(data, /*27*/14 + strlen((s8 *)productserid) + 1);
	if(u8result != 0){
		user_debug("i:SVR_logon send error:%d", u8result);
		return u8result;
	}
	time = 0;
	while(1){
		if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen)){
			if(0x03 == cmd1 && 0x01 == cmd2 && 0x01 == datalen){
				if(0x00 == *dataread)return 0;
				else{
				 user_debug("i:SVR_logon rec error:%d", *dataread);
				 return 0x7f;
				}
			}
		}
		time ++;
		if(time > 2000){
			user_debug("i:SVR_logon rec overtime");
			return 1;
		}
		eat_sleep(5);
	}
	return 2;
}

/*
*登录
* tel(11byts) + id(16byts)
**************************/
u8 SVR_enter(void)
{
	u8 data[46],*ver;
	u8 u8result, *productserid;
	u8 cmd1,cmd2,*dataread;
	u32 datalen;
	u32 time;
	u16 u16t1;
	
	memset(data, 0, 32);
	data[0] = 0x03;
	data[1] = 0x02;
	memcpy((s8 *)&data[2], "12345678901", 12);
	productserid = m2m_imeiget();//用 IMEI作为设备ID使用
	memcpy((s8 *)&data[14], productserid, strlen((s8 *)productserid) + 1);
	datalen = 14 + strlen((s8 *)productserid) + 1;
	
	//2015/12/31 10:02 fangcuisong
	data[datalen ++] = 0;
	data[datalen ++] = 2;
	data[datalen ++] = 0x10;
	u16t1 = 0;
	ver = db_update_fileget(0x10);
	if(ver)
	{
		if(*(ver + 0) >= '0' && *(ver + 0) <= '9')u16t1 = *(ver + 0) - '0';
		if(*(ver + 1) >= '0' && *(ver + 1) <= '9')u16t1 = (u16t1 << 4) + *(ver + 1) - '0';
		if(*(ver + 2) >= '0' && *(ver + 2) <= '9')u16t1 = (u16t1 << 4) + *(ver + 2) - '0';
		if(*(ver + 3) >= '0' && *(ver + 3) <= '9')u16t1 = (u16t1 << 4) + *(ver + 3) - '0';
	}
	data[datalen ++] = (u16t1 >> 8) & 0x00ff;
	data[datalen ++] = (u16t1 >> 0) & 0x00ff;
	
	data[datalen ++] = 0x0c;
	u16t1 = 0;
	ver = db_update_fileget(0x0c);
	if(ver)
	{
		if(*(ver + 0) >= '0' && *(ver + 0) <= '9')u16t1 = *(ver + 0) - '0';
		if(*(ver + 1) >= '0' && *(ver + 1) <= '9')u16t1 = (u16t1 << 4) + *(ver + 1) - '0';
		if(*(ver + 2) >= '0' && *(ver + 2) <= '9')u16t1 = (u16t1 << 4) + *(ver + 2) - '0';
		if(*(ver + 3) >= '0' && *(ver + 3) <= '9')u16t1 = (u16t1 << 4) + *(ver + 3) - '0';
	}
	data[datalen ++] = (u16t1 >> 8) & 0x00ff;;
	data[datalen ++] = (u16t1 >> 0) & 0x00ff;;
	
	u8result = SVR_FrameSend(data, datalen);
	//memcpy((s8 *)&data[14], "1405250028SI",13);
	//u8result = SVR_FrameSend(data, 27);
	if(u8result != 0)return u8result;
	time = 0;
	while(1)
	{
		if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen))
		{
			if(0x03 == cmd1 && 0x02 == cmd2 && 0x01 == datalen)
			{
				if(0x00 == *dataread)return 0;
				else return 0x7f;
			}
		}
		time ++;
		if(time > 2000)return 1;
		eat_sleep(5);
	}
	return 1;
}


/*
*同步时间
* 
**************************/
u8 SVR_system_synchron(void){
	u8 data[32];
	u8 u8result;
	u8 cmd1,cmd2,*dataread;
	u32 datalen;
	u32 time;
	
	user_infor("e:SVR_system_synchron..");
	memset(data, 0, 32);
	data[0] = 0x09;
	data[1] = 0x01;
	u8result = SVR_FrameSend(data, 2);
	if(u8result != 0)return u8result;
	time = 0;
	while(1){
		if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen)){
			if(0x09 == cmd1 && 0x01 == cmd2){
				G_system_time = *dataread;
				G_system_time = (G_system_time << 8) + *(dataread + 1);
				G_system_time = (G_system_time << 8) + *(dataread + 2);
				G_system_time = (G_system_time << 8) + *(dataread + 3);
				user_infor("e:systemtime:%x", G_system_time);
				return 0;
			}
		}
		time ++;
		if(time > 2000)return 1;
		eat_sleep(5);
	}
	return 2;
}

u32 SVR_LicenseGet(u8 *cmd, u32 cmdlen, u32 stime){
	u8 u8result, loop;
	u16 datalenu16,u16t1;
	u32 time,time1;
	u8 *dataread;
	u32 framelen,datalen,dataindex;
	
	if(NULL == cmd || cmdlen < 2)return 1;
	if(21988 == db_svr_port1get());
	else{
		  for(datalen = cmdlen; datalen >= 7; datalen --){
		  	*(cmd + datalen) = *(cmd + datalen - 1);
		  }
	    *(cmd + 6) = 0;
	    *(cmd + 3) = (*(cmd + 3)) + 1;
	    *(cmd + cmdlen) = (*(cmd + cmdlen)) + 1;
	    cmdlen ++;
  }
	G_system_time_get();
	stime = G_system_time;
	user_debug("i:SVR_LicenseGet>>[%d-%02x-%02x-%02x-%d]", cmdlen, *cmd, *(cmd + 4), *(cmd + 5), stime);
	for(loop = 0; loop < 3; loop ++){
	    for(u16t1 = 0; u16t1 < 3; u16t1 ++){
	        u8result = SVR_FrameSendEx(cmd, cmdlen, stime);
	        if(0 == u8result)break;
	        else if(2 == u8result){
	        	eat_sleep(500);
	        	continue;
	        }
	        else{
	        	user_debug("i:SVR_LicenseGet cmdsend error");
	        	return 1;
	        }
      }
      if(u16t1 >= 3){
      	user_debug("i:SVR_LicenseGet cmdsend error tcpbusy");
	      return 2;
      }
	    time = 0;
	    dataindex = 0;
	    while(1){
	    	datalenu16 = MDM_DataToApp(&dataread);
	    	if(datalenu16 > 100){
	    		for(u16t1 = 0; u16t1 < datalenu16; u16t1 ++){
	    			if(SER_FRAME_FMT1 == *(dataread + u16t1) && SER_FRAME_FMT2 == *(dataread + u16t1 + 1)){
	    		      break;
	    	    }
	    		}
	    		u16t1 += 2;
	    		framelen = *(dataread + u16t1 ++);
	    		framelen = (framelen << 8) + *(dataread + u16t1 ++);
	    		if(0x84 == *(dataread + u16t1) && 0x10 == *(dataread + u16t1 + 1) && 1024 == framelen - 4){
	    			u16t1 += 2;
	    			u16t1 += 2;//跳过2个无效的长度字节
	    			datalen = framelen - 4;
	    			update_start(datalen, UPDATE_OBD_LICENSE, (u8 *)"Lincese");
	    			user_infor("e:SVR_LicenseGet:[%d-%d-%d-%d]",framelen,datalen,u16t1,datalenu16);
	    		}
	    		else{
	    			 user_infor("e:SVR_LicenseGet:[%d-%02x-%02x]",framelen,*(dataread + u16t1),*(dataread + u16t1 +1));
	    			 datalen = 0;
	    		}
	    		
	    		if(u16t1 < datalenu16 && (datalen + 4) == framelen){
	    			dataindex = 0;
	    			dataindex = datalenu16 - u16t1;
	    			update_datain(dataread + u16t1, dataindex);
	    			time1 = 0;
	    			while(1){
	    				eat_sleep(5);
	    				time1 ++;
	    				if(time1 > 200){
	    					user_debug("i:SVR_LicenseGet overtime1");
	    					return 2;
	    				}
	    				datalenu16 = MDM_DataToApp(&dataread);
	    				if(datalenu16){
	    					  time1 = 0;
	    					  user_infor("e:SVR_LicenseGet[%d-%d]",dataindex, datalenu16);
	    					  if(dataindex + datalenu16 > 1024)datalenu16 = 1024 - dataindex;
	    					  update_datain(dataread, datalenu16);
	    					  dataindex += datalenu16;
	    			  }
	    			  if(dataindex >= 1024){//数据接收完成
	    			  	return update_obdlicense();
	    			  }
	    			}
	    		}
	    	}
	    	else if(datalenu16 >= 6)SVR_FameDeal(dataread,datalenu16);
	    	time ++;
	    	if(time > 1000){
	    		user_debug("i:SVR_LicenseGet overtime[%d]",dataindex);
	    		break;
	    	}
	    	eat_sleep(5);
	    }
	  }
	return 2;
}

/*获取星历数据
*/
u32 SVR_gps_online(u8 *gpsdata, u32 lat, u32 log)
{
	u8 data[32];
	u8 u8result;
	u8 *dataread;
	u16 datalenu16,u16t1;
	u32 framelen,datalen,dataindex;
	u32 time,time1;
	
	if(NULL == gpsdata  || 0 == lat || 0 == log)return 0;
	user_infor("e:SVR_gps_online..");
	m2m_gprslink();
	if(m2m_status() < 5)return 0;
	memset(data, 0, 32);
	data[0] = 0x14;
	data[1] = 0x20;
	data[2] = (lat >> 24 )& 0x00ff;
	data[3] = (lat >> 16 )& 0x00ff;
	data[4] = (lat >> 8 )& 0x00ff;
	data[5] = (lat >> 0 )& 0x00ff;
	data[6] = (log >> 24 )& 0x00ff;
	data[7] = (log >> 16 )& 0x00ff;
	data[8] = (log >> 8 )& 0x00ff;
	data[9] = (log >> 0)& 0x00ff;
	u8result = SVR_FrameSend(data, 10);
	if(u8result != 0)return 0;
	time = 0;
	while(1){
		datalenu16 = MDM_DataToApp(&dataread);
		if(datalenu16 > 100){
			for(u16t1 = 0; u16t1 < datalenu16; u16t1 ++){
				if(SER_FRAME_FMT1 == *(dataread + u16t1) && SER_FRAME_FMT2 == *(dataread + u16t1 + 1)){
			      break;
		    }
			}
			u16t1 += 2;
			framelen = *(dataread + u16t1 ++);
			framelen = (framelen << 8) + *(dataread + u16t1 ++);
			if(0x14 == *(dataread + u16t1) && 0x20 == *(dataread + u16t1 + 1)){
				u16t1 += 2;
				u16t1 += 2;//跳过2个无效的长度字节
				datalen = framelen - 4;
				//datalen = *(dataread + u16t1 ++);
			  //datalen = (datalen << 8) + *(dataread + u16t1 ++);
			}
			else datalen = 0;
			user_debug("i:SVR_gps_online:[%d-%d-%d-%d]",framelen,datalen,u16t1,datalenu16);
			if(u16t1 < datalenu16 && (datalen + 4) == framelen){
				dataindex = 0;
				for(; u16t1 < datalenu16; u16t1 ++){
					*(gpsdata + dataindex ++) = *(dataread + u16t1);
				}
				time1 = 0;
				while(1){
					eat_sleep(5);
					time1 ++;
					if(time1 > 200){
						user_debug("i:SVR_gps_online overtime1");
						return 0;
					}
					datalenu16 = MDM_DataToApp(&dataread);
					if(datalenu16){
						  time1 = 0;
						  user_debug("i:SVR_gps_online[%d-%d]",dataindex, datalenu16);
					    for(u16t1 = 0; u16t1 < datalenu16; u16t1 ++){
					       *(gpsdata + dataindex) = *(dataread + u16t1);
					       dataindex ++;
					       if(dataindex >= datalen)return dataindex;
				      }
				  }
				}
			}
		}
		time ++;
		if(time > 2000){
			user_debug("i:SVR_gps_online overtime");
			return 0;
		}
		eat_sleep(5);
	}
	return 0;
}

/*获取星历数据  设备已经连接到至高服务器 不需要重新连接
*/
u32 SVR_gps_onlineEx(u8 *gpsdata, u32 lat, u32 log){
	u8 data[32];
	u8 u8result,u8index;
	u8 *dataread;
	u16 datalenu16,u16t1;
	u32 framelen,datalen,dataindex;
	u32 time,time1;
	
	if(NULL == gpsdata  || 0 == lat || 0 == log)return 0;
	user_infor("e:SVR_gps_online..");
	memset(data, 0, 32);
	data[0] = 0x14;
	data[1] = 0x20;
	data[2] = (lat >> 24 )& 0x00ff;
	data[3] = (lat >> 16 )& 0x00ff;
	data[4] = (lat >> 8 )& 0x00ff;
	data[5] = (lat >> 0 )& 0x00ff;
	data[6] = (log >> 24 )& 0x00ff;
	data[7] = (log >> 16 )& 0x00ff;
	data[8] = (log >> 8 )& 0x00ff;
	data[9] = (log >> 0)& 0x00ff;
	for(u8index = 0; u8index < 10; u8index ++){
		u8result = SVR_FrameSend(data, 10);
		if(0 == u8result)break;
	  else if(2 == u8result)eat_sleep(500);
	  else{
	  	return 0;
	  }
	}
	if(u8index >= 10){
		user_debug("i:SVR_gps_online error");
		return 0;
	}
	time = 0;
	while(1){
		datalenu16 = MDM_DataToApp(&dataread);
		if(datalenu16 > 100){
			for(u16t1 = 0; u16t1 < datalenu16; u16t1 ++){
				if(SER_FRAME_FMT1 == *(dataread + u16t1) && SER_FRAME_FMT2 == *(dataread + u16t1 + 1)){
			      break;
		    }
			}
			u16t1 += 2;
			framelen = *(dataread + u16t1 ++);
			framelen = (framelen << 8) + *(dataread + u16t1 ++);
			if(0x14 == *(dataread + u16t1) && 0x20 == *(dataread + u16t1 + 1)){
				u16t1 += 2;
				u16t1 += 2;//跳过2个无效的长度字节
				datalen = framelen - 4;
				//datalen = *(dataread + u16t1 ++);
			  //datalen = (datalen << 8) + *(dataread + u16t1 ++);
			  user_infor("e:SVR_gps_online:[%d-%d-%d-%d]",framelen,datalen,u16t1,datalenu16);
			}
			else{
				user_infor("e:SVR_gps_online:[%d-%02x-%02x]",framelen,*(dataread + u16t1),*(dataread + u16t1 + 1));
				 datalen = 0;
			}
			
			if(u16t1 < datalenu16 && (datalen + 4) == framelen){
				dataindex = 0;
				for(; u16t1 < datalenu16; u16t1 ++){
					*(gpsdata + dataindex ++) = *(dataread + u16t1);
				}
				time1 = 0;
				while(1){
					eat_sleep(5);
					time1 ++;
					if(time1 > 200){
						user_debug("i:SVR_gps_online overtime1");
						return 0;
					}
					datalenu16 = MDM_DataToApp(&dataread);
					if(datalenu16){
						  time1 = 0;
						  user_debug("i:SVR_gps_online[%d-%d-%d]",datalen,dataindex, datalenu16);
					    for(u16t1 = 0; u16t1 < datalenu16; u16t1 ++){
					       *(gpsdata + dataindex) = *(dataread + u16t1);
					       dataindex ++;
					       if(dataindex >= datalen)return dataindex;
				      }
				  }
				}
			}
		}
		time ++;
		if(time > 2000){
			user_debug("i:SVR_gps_online overtime");
			return 0;
		}
		eat_sleep(5);
	}
	return 0;
}

/*
*获取基站数据
*该接口只是使用AT指令来获取基站ID信息并将数据推送到GPS缓冲区
*由APPUSER1线程通知GPS线程进行数据处理
*************************************************/
u8 SVR_mobileinforGet(void){
	return AT_CENG();
}

u8 SVR_rout_save(u32 starttime, u32 distance, u32 fuel){
	u8 back[32],back1[32],index,index1;
	u8 u8result,loop,cs;
	u8 cmd1,cmd2,*dataread;
	u32 datalen;
	u32 time;
	
	user_debug("i:SVR_rout_save[%d-%d]", distance, gps_status_get());
	if(distance < 2 || gps_status_get() != 2)return 0; //保存行程前提是：GPS有效且车辆已经行驶
	index = 0;
	back[index ++] = 0x05;
	back[index ++] = 0x01;
	back[index ++] = (starttime >> 24) & 0x00ff;
	back[index ++] = (starttime >> 16) & 0x00ff;
	back[index ++] = (starttime >> 8) & 0x00ff;
	back[index ++] = (starttime >> 0) & 0x00ff;
	back[index ++] = 0x95;
	back[index ++] = (distance >> 24) & 0x00ff;
	back[index ++] = (distance >> 16) & 0x00ff;
	back[index ++] = (distance >> 8) & 0x00ff;
	back[index ++] = (distance >> 0) & 0x00ff;
	back[index ++] = 0x8c;
	back[index ++] = (fuel >> 24) & 0x00ff;
	back[index ++] = (fuel >> 16) & 0x00ff;
	back[index ++] = (fuel >> 8) & 0x00ff;
	back[index ++] = (fuel >> 0) & 0x00ff;
	
  
  index1 = 0;
  back1[index1 ++] = SER_FRAME_FMT1;
  back1[index1 ++] = SER_FRAME_FMT2;
  G_system_time_get();
	back1[index1 ++] = (G_system_time >> 24) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 16) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 8) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 0) & 0x00ff;
	back1[index1 ++] = 0;
	cs = back1[index1 - 1];
	back1[index1 ++] = index;
	cs += back1[index1 - 1];
	for(loop = 0; loop < index; loop ++){
		back1[index1 ++] = back[loop];
		cs += back[loop];
	}
	back1[index1 ++] = cs;
  rout_filesave(back1, index1);
  return 1;
}


u8 SVR_rout_tosvr(u8 *data, u32 datainlen)
{
	u8 back[128],back1[128],index,index1;
	u8 u8result,loop,cs;
	u8 cmd1,cmd2,*dataread;
	u32 datalen;
	u32 time,lac,loc;
	
	if(NULL == data || datainlen > 120)return 1;
	memcpy(back, data, datainlen);
	index = datainlen;
	
	user_debug("i:SVR_rout_tosvr=%d", datainlen);
	if(m2m_status() >= 7)
	{//如果M2M未与服务器建立连接 则直接保存
		for(loop = 0; loop < 2; loop ++)
		{
	    		u8result = SVR_FrameSend(back, index);
	    		if(u8result != 0);//通讯异常 需要保存
	    		else
			{
	        		time = 0;
	        		while(1)
				{
	        			if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen))
					{
	        				if(0x05 == cmd1 & 0x01 == cmd2)
						{
	        					return 0;
	        				}
	        			}
	        			time ++;
	        			if(time > 800)break;//通讯异常  数据需要保存
	        			eat_sleep(5);
	        		}
	    		}
	  	}
  	}
  	user_debug("i:SVR_rout error need to save[%d]",m2m_status());
  	index1 = 0;
  	back1[index1 ++] = SER_FRAME_FMT1;
  	back1[index1 ++] = SER_FRAME_FMT2;
  	G_system_time_get();
	back1[index1 ++] = (G_system_time >> 24) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 16) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 8) & 0x00ff;
	back1[index1 ++] = (G_system_time >> 0) & 0x00ff;
	back1[index1 ++] = 0;
	cs = back1[index1 - 1];
	back1[index1 ++] = index;
	cs += back1[index1 - 1];
	for(loop = 0; loop < index; loop ++)
	{
		back1[index1 ++] = back[loop];
		cs += back[loop];
	}
	back1[index1 ++] = cs;
  	rout_filesave(back1, index1);
  	return 0;
}


/*
*终端上报行程 一个行程结束后终端需要上报行程
*数据来Flash
*************************************************************/
u8 SVR_routEx(void)
{
	u8 *back,back1[128],index1;
	u16 index,u16temp;
	u8 u8result,loop;
	u8 cmd1,cmd2,*dataread;
	u32 datalen;
	u32 time;
	
	index = 0;
	//user_debug("SVR_routEx>>");
	index = rout_fileread(&back);
	if(0 == index)return 0;
	//user_debug("SVR_routEx[%d]",index);
	u16temp = 0;
	while(1)
	{//如果有多个行程记录 需要一次性奖所有行程发送到服务器
		  if(*(back + u16temp) != SER_FRAME_FMT1 || *(back + u16temp + 1) != SER_FRAME_FMT2)break;
		  for(index1 = 0; index1 < 128; index1 ++)
		  {
		  	back1[index1] = *(back + u16temp);
		  	u16temp ++;
		  	if(SER_FRAME_FMT1 == *(back + u16temp) && SER_FRAME_FMT2 == *(back + u16temp + 1))
			{
		  		index1 ++;
		  		break;
		  	}
		  	if(u16temp >= index)
			{
		  		index1 ++;
		  		break;
		  	}
		  }
		  if(index1 >= 128)return 1;
		  user_debug("i:lilei-send history rout 05 01\r\n");
		  AT_CIPSEND(back1, index1);
	    
	    	time = 0;
	    	while(1)
		{
	    		if(0 == SVR_FrameRead(&cmd1, &cmd2, &dataread, &datalen))
			{
	    			if(0x05 == cmd1 & 0x01 == cmd2)
				{
	    				break;
	    			}
	    		}
	    		time ++;
	    		if(time > 1000)
			{
	    			rout_filesave(back + u16temp, index - u16temp);
	    			return 2;//通讯异常  数据需要保存
	    		}
	    		eat_sleep(5);
	    	}
	    	if(u16temp >= index)return 0;
 	 }
  	user_debug("i:SVR_routEx error");
  	return 1;
}

/*
*发送升级请求
*M2M文件在每次启动时发送升级请求
*OBD文件在每次进入睡眠前发送升级请求
*/
extern void MDM_read_debugset(void);
u8 SVR_Cmd84(u8 type){
	u8 back[5], sendindex;
	
	//MDM_read_debugset();
	memset(back, 0, 5);
	back[0] = 0x84;
	back[1] = 0x07;
	back[2] = 0;
	back[3] = type;
	for(sendindex = 0; sendindex < 5; sendindex ++){
	    if(0 == SVR_FrameSend(back, 4))break;
	    eat_sleep(400);
  }
  user_debug("i:SVR_Cmd84:[%d-%d]",type,sendindex);
}

/*发送指令到服务器 必须等到服务器的应答
*/
u8 SVR_cmdxx_Ex(u8 *cmd, u32 cmdlen, u32 timeflag, u8 cmd1){
	u8 loop,u8result;
	u8 cmdmain,cmdsub,*dataread;
	u32 datalen,time;
	
	user_debug("i:SVR_cmdxx_Ex=[%02x-%d]", cmd1, cmdlen);
	if(NULL == cmd || cmdlen < 5)return 1;
	for(loop = 0; loop < 3; loop ++){
		u8result =  SVR_FrameSendEx(cmd, cmdlen, timeflag);
		if(0 == u8result){
			 time = 0;
	     while(1){
	     	   if(0 == SVR_FrameRead(&cmdmain, &cmdsub, &dataread, &datalen)){
	     	   	  if(cmd1 == cmdmain){
	     	   		   return 0;
	     	   	  }
	     	   }
	     	   time ++;
	     	   if(time > 800)break;//通讯异常  数据需要保存
	     	   eat_sleep(5);
	     }
		}
		else{
			eat_sleep(3000);
		}
	}
	user_debug("i:SVR_cmdxx_Ex overtime");
	return 2;
}

/*************************************************************************/
/*将服务器发来的OBD请求数据发送到OBD模块
*/
u8 SVR_Cmd11(u8 cmd2, u8 *data, u32 datalen){
	u32 port;
	u8 addrindex, addrlen;
	u8 datasend[3];
	u8 ip[128];
	u8 type;
	
	datasend[0] = 0x11;
	datasend[1] = 0x01;
	if(cmd2 != 1 || NULL == data || datalen > 250 || datalen < 11){
		SVR_back7f(0x11, 4);
		user_debug("i:SVR_Cmd11 cmd-error");
		return 1;
	}
	
	type = *(data + 6);//地址类型 1=主服务器 2=辅助服务器
	if(1 == type || 2 == type);
	else{
		user_debug("i:SVR_Cmd11 server IP error[%d]",type);
		datasend[2] = 1;
	  SVR_FrameSend(datasend, 3);
		return 1;
	}
	memset(ip, 0, 128);
	addrlen = 0;
	for(addrindex = 7; addrindex < datalen -6; addrindex ++){
		if(0 == *(data + addrindex))break;
		ip[addrlen ++] = *(data + addrindex);
		if(addrlen >= 128){
			user_debug("i:SVR_Cmd11 server IP len error[%d]",datalen);
			datasend[2] = 1;
	    SVR_FrameSend(datasend, 3);
		  return 2;
		}
	}
	port = *(data + addrindex + 1);
  port = (port << 8) + *(data + addrindex + 2);
  port = (port << 8) + *(data + addrindex + 3);
  port = (port << 8) + *(data + addrindex + 4);
  
  if(2 == type){//验证IP端口是否在正确  
  	 if(0 == AT_CIPSTART_test(ip, port)){
  	  	user_debug("i:ip2 set error[%s, %d]", ip, port);
  	  	datasend[2] = 1;
	      SVR_FrameSend(datasend, 3);
  	  	return 3;
  	 }
  	 if(SVR_logon_test() != 0){
  	 	user_debug("i:ip2 set check error[%s, %d]", ip, port);
  	 	AT_CIPCLOSE_test();
  	 	datasend[2] = 1;
	    SVR_FrameSend(datasend, 3);
  	  return 3;
  	}
  	AT_CIPCLOSE_test();
  }
  
  if(1 == type){
  	db_svr_addrset(ip);
  	db_svr_portset(port);
  }
	else{
		 db_svr_addr1set(ip);
		 db_svr_port1set(port);
	}
  
  
  user_debug("i:SVR_Cmd11 OK:%s [%d-%d][%02x-%02x-%02x-%02x-%02x]", ip, type, port,*(ip+0),*(ip+1),*(ip+2),*(ip+3),*(ip+4));
  
  if(0 == db_svr_save()){//同步到数据库
    datasend[2] = 0;
	  SVR_FrameSend(datasend, 3);
  }
	//设置服务器IP以及端口后需要重新连接
	if(2 == type)m2m_svr_addrtemplerset(db_svr_addr1get(), db_svr_port1get());
	m2m_statusSet(4);//a5 a5 len1 len2 11 01 0
	
  return 0;
}

/*设置固定号码
*
**************************************************/
u8 SVR_Cmd12(u8 cmd2, u8 *data, u32 datalen){
	u8 telnum,tellen;
	u32 dataindex;
	u8 tel[24];
	u8 datasend[4];
	
	if(cmd2 != 1 || NULL == data || datalen > 250 || datalen < 17){
		SVR_back7f(0x12, 4);
		user_debug("i:SVR_Cmd12 cmd-error");
		return 0;
	}
	telnum = 0;
	tellen = 0;
	memset(tel, 0 ,24);
	for(dataindex = 6; dataindex < datalen - 1; dataindex ++){
		if(0 == *(data + dataindex)){
			if(tellen > 2){
				db_svr_telxset(tel, telnum);
			}
			memset(tel, 0 ,24);
			tellen = 0;
			telnum ++;
			if(telnum >= 5){
				break;
			}
		}
		tel[tellen] = *(data + dataindex);
		tellen ++;
		if(tellen >= 24){
			SVR_back7f(0x12, 4);
	  	user_debug("i:SVR_Cmd12 tel-error");
		  return 0;
		}
	}
	datasend[0] = 0x12;
	datasend[1] = 0x01;
	datasend[2] = 0x00;
	SVR_FrameSend(datasend, 3);
	user_infor("e:SVR_Cmd12 OK");
	return 0;
}
/*
*处理服务器发来的数据
*有些数据针对OBD模块
*有些数据是针对M2M模块
*data: a5 a5 L1 L2 CMD1 CMD2 data cs
*****************************************************/
u8 SVR_Cmd(u8 cmd1, u8 cmd2, u8 *data, u32 datalen){
	u8 back[32];
	u8 BackFlag=1;
	
	//user_debug("SVR_Cmd>>>>>>>>>:[%02x-%02x]", cmd1,cmd2);
	if(0x01 == cmd1)
	{//保持
	}
	else if(0x11 == cmd1)
	{//平台参数设置
		if(0x00 == SVR_Cmd11(cmd2, data, datalen))
		{//设置IP PORT
		}
		else SVR_back7f(0x11, 3);
	}
	else if(0x12 == cmd1)
	{//设置固定号码
		SVR_Cmd12(cmd2, data, datalen);
	}
	else if(0x14 == cmd1)
	{//设置数据采集 、上传周期 当前全部采用默认 由终端设备决定
	}
	else if(0x13 == cmd1 && 0x05 == cmd2)
	{
		back[0] = cmd1;
		back[1] = cmd2;
		back[2] = 0;
		if(0x00 == *(data + 6))hw_th_unable_set(0);//解除硬件防盗
		else hw_th_unable_set(1);
		SVR_FrameSend(back, 3);
	}
	else if(0x84 == cmd1)
	{//升级
		obd_cmd84(cmd2, data, datalen);
	} 
	else if((cmd1==0x8d)&&(cmd2==0x0d))         //add by lilei-2016-平台设置超速参数
	{
		obd_writeEx(data, datalen);//透传到OBD

		
	}
	else if((cmd1==0x8d)&&(cmd2==0x0e))         //add by lilei-2016-平台设置疲劳驾驶时间参数
	{
		obd_writeEx(data, datalen);//透传到OBD

	}
	else if((cmd1==0x8d)&&(cmd2==0x0f))         //add by lilei-2016-平台设置怠速时间参数
	{
		obd_writeEx(data, datalen);//透传到OBD
	}
	else if((cmd1==0x8d)&&(cmd2==0x13))         //add by lilei-2016-平台下发报警
	{

	    back[0] = cmd1;
	    back[1] = cmd2;
		
	    user_debug("\r\nRecive-Svr-8d-13 voice-");
           if(data[6]==0)
           {
           	    user_debug("OverSpeed\r\n");
		    AT_CREC("C:\\User\\Over-Speed.amr", 99); 
		  
	     }
	     else if(data[6]==1)
	     {
	     	   user_debug("TiredDrive\r\n");
		   AT_CREC("C:\\User\\Tired-Drive.amr", 99); 
		 
	     }
            else if(data[6]==2)
	     {
	     	   user_debug("IdlSpeed\r\n");
		   AT_CREC("C:\\User\\Idle-Speed.amr", 99); 
	     }
	     else if(data[6]==3)
	     {
	     	    user_debug("SensitiveArea\r\n");
		   AT_CREC("C:\\User\\Sensitive-Area.amr", 99); 
	     }
	     else
	     {
			BackFlag=0;
	     }
	     if(BackFlag)
	     {
			 back[2]=0;
			 SVR_FrameSend(back, 3);

	     }

	}
	else
	{
		 if(CMD_VEHICLESET == cmd1 && 0x03 == cmd2)
		 {//重新设置总里程
		 	db_obd_reset();
		 }
		 obd_writeEx(data, datalen);//透传到OBD
	}
	
	
	//if(0x80 == cmd1)eat_sleep(200);//如果当前指令为设置指令 数据发送到OBD后延时200ms
	return 0;
}


void SVR_test(u8 type){
	u8 testdata[256];
	u32 dataindex,u32t1;
	
	if(1 == type){//设置号码
		testdata[0] = 0xa5;
		testdata[1] = 0xa5;
		testdata[4] = 0x12;
		testdata[5] = 0x01;
		strcpy((s8* )&testdata[6], "13728716114");
		dataindex = 17;
		testdata[dataindex ++] = 0;
		testdata[dataindex ++] = 0;
		strcpy((s8* )&testdata[dataindex], "15768278973");
		dataindex += 11;
		testdata[dataindex ++] = 0;
		strcpy((s8* )&testdata[dataindex], "15768278900");
		dataindex += 11;
		testdata[dataindex ++] = 0;
		testdata[dataindex ++] = 0;
		testdata[dataindex ++] = 1;
		testdata[2] = (dataindex >> 8) & 0x00ff;
		testdata[3] = (dataindex >> 0) & 0x00ff;
		SVR_Cmd12(0x01, testdata, dataindex);
	}
	if(2 == type){//设置网络地址 IP
		testdata[0] = 0xa5;
		testdata[1] = 0xa5;
		testdata[4] = 0x11;
		testdata[5] = 0x01;
		strcpy((s8* )&testdata[6], "123.58.32.89");
		dataindex = 20;
		testdata[dataindex ++] = 0;
		u32t1 = 21989;
		testdata[dataindex ++] = (u32t1 >> 24) & 0x00ff;
		testdata[dataindex ++] = (u32t1 >> 16) & 0x00ff;
		testdata[dataindex ++] = (u32t1 >> 8) & 0x00ff;
		testdata[dataindex ++] = (u32t1 >> 0) & 0x00ff;
		testdata[dataindex ++] = 1;
		testdata[2] = (dataindex >> 8) & 0x00ff;
		testdata[3] = (dataindex >> 0) & 0x00ff;
		SVR_Cmd11(0x01, testdata, dataindex);
	}
}

/*
*读取并返回bigmem中的数据
****************************************************/
u16 SVR_Bigmem_send(void){//一级缓冲中的数据
	
	return bigmem_obdgps_out(SEV_APP_BUF);
}

/*服务器切换
*该接口只完成设备服务器地址的切换 进行登入 不在做注册时间同步等操作
*/
u8 SVR_SvrChange(u8 *addr, u32 port){
	u8 u8result;
	
	if(NULL == addr || 0 == port)return 0;
	u8result = m2m_ser_addrcheck(addr);
	if(2 == u8result || 3 == u8result){//地址不同需要重新连接服务器 重新登入
		m2m_svr_addrtemplerset(addr, port);
		m2m_gprslink();
		eat_sleep(100);
		u8result = SVR_enter();
		return u8result;
	}
	
	return 0;
}



u32 ydmhms2u32(u32 ymd, u32 time){
 u32 times, timess;
 u8 data;
 
 if(0 == ymd)return 0;
 data = (ymd >> 0) & 0x003f;
 if(data < 14)return 0;
 timess = (data << 26);
 data = (ymd >> 8) & 0x0f;
 if(data > 12)return 0;
 timess |= (data << 22);
 data = (ymd >> 16) & 0x1f;
 if(data > 31)return 0;
 timess |= (data << 17);
 times = time & 0x00ff;
 times += ((time >> 8) & 0x00ff) * 60;
 times += ((time >> 16) & 0x00ff) * 60 * 60;
 timess += times;
 
 //user_debug("GPS_TTTT=%d[%d,%d]", timess, ymd, time); 
 Lstime_gps_update(timess);
 return timess;
}


