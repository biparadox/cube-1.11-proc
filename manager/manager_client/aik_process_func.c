#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>

#include "../include/data_type.h"
#include "../include/struct_deal.h"
#include "../include/crypto_func.h"
#include "../include/extern_struct.h"
#include "../include/extern_defno.h"
#include "../include/message_struct.h"
#include "../include/vmlist.h"
#include "../include/vtpm_struct.h"
#include "../include/vm_policy.h"
#include "../include/connector.h"
#include "../include/logic_baselib.h"
#include "../include/connector.h"
#include "../include/sec_entity.h"
#include "../include/tesi.h"
#include "../include/main_proc_init.h"
#include "../include/tesi_aik_struct.h"

#include "cloud_config.h"
#include "main_proc_func.h"
#include "proc_config.h"
#include "aik_process_func.h"

int print_error(char * str, int result)
{
	printf("%s %s",str,tss_err_string(result));
}

struct aik_proc_pointer
{
	TSS_HKEY hAIKey;
	TSS_HKEY hSignKey;
	TESI_SIGN_DATA * aik_cert;
};

int proc_aikclient_init(void * sub_proc,void * para)
{
	int ret;
	TSS_RESULT result;	
	char local_uuid[DIGEST_SIZE*2+1];
	
	struct aik_proc_pointer * aik_pointer;
//	main_pointer= kmalloc(sizeof(struct main_proc_pointer),GFP_KERNEL);
	aik_pointer= malloc(sizeof(struct aik_proc_pointer));
	if(aik_pointer==NULL)
		return -ENOMEM;
	memset(aik_pointer,0,sizeof(struct aik_proc_pointer));

	sec_subject_register_statelist(sub_proc,aik_state_list);

//	OpenSSL_add_all_algorithms();
//      ERR_load_crypto_strings();
	result=TESI_Local_ReloadWithAuth("ooo","sss");
	if(result!=TSS_SUCCESS)
	{
		printf("open tpm error %d!\n",result);
		return -ENFILE;
	}
	sec_subject_setstate(sub_proc,PROC_AIK_TPMOPEN);
	return 0;
}

int proc_aikclient_start(void * sub_proc,void * para)
{
	int ret;
	int retval;
	void * message_box;
	void * context;
	int i;
	struct tcloud_connector * temp_conn;

	char local_uuid[DIGEST_SIZE*2+1];
	char proc_name[DIGEST_SIZE*2+1];
	
	ret=proc_share_data_getvalue("uuid",local_uuid);
	ret=proc_share_data_getvalue("proc_name",proc_name);

	printf("begin aik process start!\n");

	for(i=0;i<300*1000;i++)
	{
		usleep(time_val.tv_usec);
		ret=sec_subject_recvmsg(sub_proc,&message_box);
		if(ret<0)
			continue;
		if(message_box==NULL)
			continue;
		MESSAGE_HEAD * msg_head;
		msg_head=get_message_head(message_box);
		if(msg_head==NULL)
			continue;
		if(strncmp(msg_head->record_type,"SYNI",4)==0)
		{
			proc_aik_request(sub_proc,message_box,NULL);
		}
		else if(strncmp(msg_head->record_type,"FILD",4)==0)
		{
			proc_aik_activate(sub_proc,message_box,NULL);
		}
	}

	return 0;
};


int proc_aik_request(void * sub_proc,void * message,void * pointer)
{
	TSS_RESULT result;
	TSS_HKEY 	hSignKey;
	TSS_HKEY	hAIKey, hCAKey;
	struct aik_request_info reqinfo;
	struct policyfile_data * reqdata;
	int ret;

	BYTE		*labelString = "UserA";
	UINT32		labelLen = strlen(labelString) + 1;
	char local_uuid[DIGEST_SIZE*2+1];
	char proc_name[DIGEST_SIZE*2+1];
	
	ret=proc_share_data_getvalue("uuid",local_uuid);
	ret=proc_share_data_getvalue("proc_name",proc_name);

	printf("begin aik request!\n");
//	result=TESI_Local_ReloadWithAuth("ooo","sss");
//	if(result!=TSS_SUCCESS)
//	{
//		printf("open tpm error %d!\n",result);
//		return -ENFILE;
//	}
	char buffer[1024];
	char digest[DIGEST_SIZE];
	int blobsize=0;
	int fd;
	// create a signkey and write its key in localsignkey.key, write its pubkey in localsignkey.pem
	result=TESI_Local_ReloadWithAuth("ooo","sss");
	result=TESI_Local_CreateSignKey(&hSignKey,(TSS_HKEY)NULL,"sss","kkk");
	if(result == TSS_SUCCESS)
		printf("Create SignKey SUCCEED!\n");

	TESI_Local_WriteKeyBlob(hSignKey,"privkey/localsignkey");
	TESI_Local_WritePubKey(hSignKey,"pubkey/localsignkey");
	
	// fill the reqinfo struct
	calculate_sm3("pubkey/localsignkey.pem",digest);
	digest_to_uuid(digest,reqinfo.signpubkey_uuid);
	calculate_sm3("pubkey/pubek.pem",digest);
	digest_to_uuid(digest,reqinfo.pubek_uuid);
	reqinfo.user_name=labelString;
	get_local_uuid(reqinfo.user_uuid);
	
	// create info blob
	void * struct_template=create_struct_template(req_info_desc);
	if(struct_template==NULL)
		return -EINVAL;
	blobsize=struct_2_blob(&reqinfo,buffer,struct_template);


	// Load the CA Key
	result=TESI_Local_GetPubKeyFromCA(&hCAKey,"cert/CA");
	if (result != TSS_SUCCESS) {
		printf("Get pubkey error %s!\n", tss_err_string(result));
		exit(result);
	}
	
	TESI_AIK_CreateIdentKey(&hAIKey,NULL,"sss","kkk"); 
	if (result != TSS_SUCCESS) {
		printf("Create AIK error %s!\n", tss_err_string(result));
		exit(result);
	}

	labelLen=strlen(labelString);

	result = TESI_AIK_GenerateReq(hCAKey,blobsize,buffer,hAIKey,"cert/aik");
	if (result != TSS_SUCCESS){
		printf("Generate aik failed%s!\n",tss_err_string(result));
		exit(result);
	}
	TESI_Local_WriteKeyBlob(hAIKey,"privkey/AIK");

//	sleep(2);
	sec_subject_setstate(sub_proc,PROC_AIK_CREATEKEY);

	ret=build_filedata_struct(&reqdata,"cert/aik.req");
	void * send_msg;
	send_msg=message_create("FILD");
	message_add_record(send_msg,reqdata);
	sec_subject_sendmsg(sub_proc,send_msg);

	printf("aik client send message succeed!\n");

	return 0;

}

int proc_aik_activate(void * sub_proc,void * message,void * pointer)
{
	printf("begin aik activate!\n");
	TSS_RESULT	result;
	TSS_HKEY	hAIKey, hCAKey;
	
	TESI_SIGN_DATA signdata;

	int fd;
	int retval;
	int blobsize=0;
	struct policyfile_data * reqdata;


	result=TESI_Local_ReadKeyBlob(&hAIKey,"privkey/AIK");
	if ( result != TSS_SUCCESS )
	{
		print_error("TESI_Local_ReadKeyBlob Err!\n",result);
		return result;
	}

	result=TESI_Local_LoadKey(hAIKey,NULL,"kkk");
	if ( result != TSS_SUCCESS )
	{
		print_error("TESI_Local_LoadKey Err!\n",result);
		return result;
	}

	retval=get_filedata_from_message(message);
	if(retval<0)
		return -EINVAL;
	printf("get file succeed!\n");

	result=TESI_AIK_Activate(hAIKey,"cert/active",&signdata);
	if (result != TSS_SUCCESS) {
		print_error("Tspi_Context_CreateObject", result);
		exit(result);
	}
	// Load the CA Key
	result=TESI_Local_GetPubKeyFromCA(&hCAKey,"cert/CA");
	if (result != TSS_SUCCESS) {
		print_error("Get pubkey error!\n", result);
		exit(result);
	}
	
	// write the AIK and aipubkey

	result=TESI_Local_WriteKeyBlob(hAIKey,"privkey/AIK");
	if (result != TSS_SUCCESS) {
		print_error("store aik data error!\n", result);
		exit(result);
	}
	result=TESI_Local_WritePubKey(hAIKey,"pubkey/AIK");
	if (result != TSS_SUCCESS) {
		print_error("store aik data error!\n", result);
		exit(result);
	}

	// verify the CA signed cert
	result=TESI_AIK_VerifySignData(&signdata,"cert/CA");
	if (result != TSS_SUCCESS) {
		print_error("verify data error!\n", result);
		exit(result);
	}

	// get the content of the CA signed cert

	struct ca_cert usercert;

	// read the req info from aik request package
	void * struct_template=create_struct_template(ca_cert_desc);
	if(struct_template==NULL)
		return -EINVAL;
	blobsize=blob_2_struct(signdata.data,&usercert,struct_template);
	if(blobsize!=signdata.datalen)
		return -EINVAL;

	free_struct_template(struct_template);

	return 0;

}