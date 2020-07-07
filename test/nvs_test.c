
/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "nvs_utility.h"
#include <string.h>

// TO TEST THE NVS ADD THE FOLLOWING STEPS ARE REQUIRED
//
// 	1. ADD THESE ITEMS TO THE NVS_Items_t enum in nvs_utility.h:
//	NVS_U8_TEST,
//	NVS_I8_TEST,
//	NVS_U16_TEST,
//	NVS_I16_TEST,
//	NVS_U32_TEST,
//	NVS_I32_TEST,
//	NVS_U64_TEST,
//	NVS_I64_TEST,
//	NVS_STR_TEST,
//	NVS_BLOB_TEST
//
//	2. ADD THE FOLLOWING ITEMS TO THE NVS_Items[] STRUCTURE IN nvs_utility.c
//	[ NVS_U8_TEST ]        = 	{.type =NVS_TYPE_U8,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestU8"},
//	[ NVS_I8_TEST ]        = 	{.type =NVS_TYPE_I8,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestI8"},
//	[ NVS_U16_TEST ]        = 	{.type =NVS_TYPE_U16,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestU16"},
//	[ NVS_I16_TEST ]        = 	{.type =NVS_TYPE_I16,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestI16"},
//	[ NVS_U32_TEST ]        = 	{.type =NVS_TYPE_U32,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestU32"},
//	[ NVS_I32_TEST ]        = 	{.type =NVS_TYPE_I32,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestI32"},
//	[ NVS_U64_TEST ]        = 	{.type =NVS_TYPE_U64,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestU64"},
//	[ NVS_I64_TEST ]        = 	{.type =NVS_TYPE_I64,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestI64"},
//	[ NVS_STR_TEST ]        = 	{.type =NVS_TYPE_STR,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestStr"},
//	[ NVS_BLOB_TEST ]        = 	{.type =NVS_TYPE_BLOB,			.partition =NVS_PART_PDATA, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "TestBlob"},
//

static void _nvsTestInt32Equals(int32_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_i32]PASSED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_i32]FAILED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestInt32NotEquals(int32_t testItem, int32_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_i32]PASSED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_i32]FAILED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
}

static void _nvsTestUInt32Equals(uint32_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_u32]PASSED %s, Expected Val: %u, Actual Val:%u\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_u32]FAILED %s, Expected Val: %u, Actual Val:%u\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestInt64Equals(int64_t testItem, int64_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_i64]PASSED %s, Expected Val: %lli, Actual Val:%lli\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_i64]FAILED %s, Expected Val: %lli, Actual Val:%lli\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestInt64NotEquals(int64_t testItem, int64_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_i64]PASSED %s, Not Equal Val: %lli, Actual Val:%lli\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_i64]FAILED %s, Not Equal Val: %lli, Actual Val:%lli\r\n", testTitle, notEqualVal, testItem));
	}
}



// ---------- NVS UINT8 TESTS ----------
static void _nvsTestUInt8Equals(uint8_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_u8]PASSED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_u8]FAILED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestUInt8NotEquals(uint8_t testItem, int32_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_u8]PASSED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_u8]FAILED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
}


static void _testU8(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	uint8_t output;
	int u8_test_high_extreme = 255;
	int u8_test_low_extreme = 0;
	int u8_ooo_high	= 257;

	err = NVS_Get(NVS_U8_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "U8 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_U8_TEST, &u8_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_U8_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestUInt32Equals(size, sizeof(uint8_t), "U8 NVS_Get_Size_Of(): size == sizeof(uint8_t)");

	err = NVS_Get(NVS_U8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Get() == ESP_OK");
	_nvsTestUInt8Equals(output, u8_test_high_extreme, "U8 NVS_Get(): output == input");

	err = NVS_Set(NVS_U8_TEST, &u8_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_U8_TEST, &u8_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_U8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Get() == ESP_OK");
	_nvsTestUInt8Equals(output, u8_test_low_extreme, "U8 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_U8_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_U8_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "U8 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_U8_TEST, &u8_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Set() == ESP_OK When trying to set 257");

	err = NVS_Get(NVS_U8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U8 NVS_Get() == ESP_OK");
	_nvsTestUInt8NotEquals(output, u8_ooo_high, "U8 NVS_Get(): output != 257");

}



// ---------- NVS INT8 TESTS ----------

static void _nvsTestInt8Equals(int8_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_i8]PASSED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_i8]FAILED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestInt8NotEquals(int8_t testItem, int32_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_i8]PASSED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_i8]FAILED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
}


static void _testI8(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	int8_t output;
	int i8_test_high_extreme = 127;
	int i8_test_low_extreme = -127;
	int i8_ooo_high	= 129;

	err = NVS_Get(NVS_I8_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "I8 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_I8_TEST, &i8_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_I8_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(int8_t), "I8 NVS_Get_Size_Of(): size == sizeof(int8_t)");

	err = NVS_Get(NVS_I8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Get() == ESP_OK");
	_nvsTestInt8Equals(output, i8_test_high_extreme, "I8 NVS_Get(): output == input");

	err = NVS_Set(NVS_I8_TEST, &i8_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_I8_TEST, &i8_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_I8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Get() == ESP_OK");
	_nvsTestInt8Equals(output, i8_test_low_extreme, "I8 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_I8_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_I8_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "I8 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_I8_TEST, &i8_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Set() == ESP_OK When trying to set 129");

	err = NVS_Get(NVS_I8_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I8 NVS_Get() == ESP_OK");
	_nvsTestInt8NotEquals(output, i8_ooo_high, "I8 NVS_Get(): output != 129");

}



// ---------- NVS UINT16 TESTS ----------
static void _nvsTestUInt16Equals(uint16_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_u16]PASSED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_u16]FAILED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestUInt16NotEquals(uint16_t testItem, int32_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_u16]PASSED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_u16]FAILED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
}


static void _testU16(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	uint16_t output;
	int u16_test_high_extreme = 65535;
	int u16_test_low_extreme = 0;
	int u16_ooo_high	= 65537;

	err = NVS_Get(NVS_U16_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "U16 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_U16_TEST, &u16_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_U16_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(uint16_t), "U16 NVS_Get_Size_Of(): size == sizeof(uint16_t)");

	err = NVS_Get(NVS_U16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Get() == ESP_OK");
	_nvsTestUInt16Equals(output, u16_test_high_extreme, "U16 NVS_Get(): output == input");

	err = NVS_Set(NVS_U16_TEST, &u16_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_U16_TEST, &u16_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_U16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Get() == ESP_OK");
	_nvsTestUInt16Equals(output, u16_test_low_extreme, "U16 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_U16_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_U16_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "U16 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_U16_TEST, &u16_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Set() == ESP_OK When trying to set OOB High");

	err = NVS_Get(NVS_U16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U16 NVS_Get() == ESP_OK");
	_nvsTestUInt16NotEquals(output, u16_ooo_high, "U16 NVS_Get(): output != OOB High");

}



// ---------- NVS INT16 TESTS ----------
static void _nvsTestInt16Equals(int16_t testItem, int32_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_i16]PASSED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_i16]FAILED %s, Expected Val: %d, Actual Val:%d\r\n", testTitle, expectedVal, testItem));
	}
}

static void _nvsTestInt16NotEquals(int16_t testItem, int32_t notEqualVal, const char* testTitle){
	if(testItem!=notEqualVal){
		configPRINTF(("[Test_i16]PASSED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
	else{
		configPRINTF(("[Test_i16]FAILED %s, Not Equal Val: %d, Actual Val:%d\r\n", testTitle, notEqualVal, testItem));
	}
}


static void _testI16(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	int16_t output;
	int i16_test_high_extreme = 32767;
	int i16_test_low_extreme = -32768;
	int i16_ooo_high	= 32769;

	err = NVS_Get(NVS_I16_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "I16 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_I16_TEST, &i16_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_I16_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(int16_t), "I16 NVS_Get_Size_Of(): size == sizeof(int16_t)");

	err = NVS_Get(NVS_I16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Get() == ESP_OK");
	_nvsTestInt16Equals(output, i16_test_high_extreme, "I16 NVS_Get(): output == input");

	err = NVS_Set(NVS_I16_TEST, &i16_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_I16_TEST, &i16_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_I16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Get() == ESP_OK");
	_nvsTestInt16Equals(output, i16_test_low_extreme, "I16 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_I16_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_I16_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "I16 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_I16_TEST, &i16_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Set() == ESP_OK When trying to set OOB High");

	err = NVS_Get(NVS_I16_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I16 NVS_Get() == ESP_OK");
	_nvsTestInt16NotEquals(output, i16_ooo_high, "U16 NVS_Get(): output != OOB High");

}



// ---------- NVS UINT32 TESTS ----------

static void _testU32(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	uint32_t output;
	uint32_t u32_test_high_extreme = 4294967295;
	uint32_t u32_test_low_extreme = 0;
	int64_t u32_ooo_high	= 0x100000001;

	err = NVS_Get(NVS_U32_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "U32 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_U32_TEST, &u32_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_U32_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(uint32_t), "U32 NVS_Get_Size_Of(): size == sizeof(uint32_t)");

	err = NVS_Get(NVS_U32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Get() == ESP_OK");
	_nvsTestUInt32Equals(output, u32_test_high_extreme, "U32 NVS_Get(): output == input");

	err = NVS_Set(NVS_U32_TEST, &u32_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_U32_TEST, &u32_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_U32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Get() == ESP_OK");
	_nvsTestUInt32Equals(output, u32_test_low_extreme, "U32 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_U32_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_U32_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "U32 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_U32_TEST, &u32_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Set() == ESP_OK When trying to set OOB High");

	err = NVS_Get(NVS_U32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U32 NVS_Get() == ESP_OK");
	_nvsTestInt64NotEquals(output, u32_ooo_high, "U32 NVS_Get(): output != OOB High");

}



// ---------- NVS INT32 TESTS ----------

static void _testI32(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	int32_t output;
	int32_t i32_test_high_extreme = 2147483647;
	int32_t i32_test_low_extreme = -2147483647;
	int64_t i32_ooo_high	= 0x100000001;

	err = NVS_Get(NVS_I32_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "I32 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_I32_TEST, &i32_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_I32_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(int32_t), "I32 NVS_Get_Size_Of(): size == sizeof(int32_t)");

	err = NVS_Get(NVS_I32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(output, i32_test_high_extreme, "I32 NVS_Get(): output == input");

	err = NVS_Set(NVS_I32_TEST, &i32_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_I32_TEST, &i32_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_I32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(output, i32_test_low_extreme, "I32 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_I32_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_I32_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "I32 NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_I32_TEST, &i32_ooo_high, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Set() == ESP_OK When trying to set OOB High");

	err = NVS_Get(NVS_I32_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I32 NVS_Get() == ESP_OK");
	_nvsTestInt64NotEquals(output, i32_ooo_high, "U32 NVS_Get(): output != OOB High");

}



// ---------- NVS UINT64 TESTS ----------
static void _nvsTestUInt64Equals(uint64_t testItem, int64_t expectedVal, const char* testTitle){
	if(testItem==expectedVal){
		configPRINTF(("[Test_u32]PASSED %s, Expected Val: %llu, Actual Val:%llu\r\n", testTitle, expectedVal, testItem));
	}
	else{
		configPRINTF(("[Test_u32]FAILED %s, Expected Val: %llu, Actual Val:%llu\r\n", testTitle, expectedVal, testItem));
	}
}

static void _testU64(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	uint64_t output;
	uint64_t u64_test_high_extreme = 18446744073709551615;
	uint64_t u64_test_low_extreme = 0;

	err = NVS_Get(NVS_U64_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "U64 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_U64_TEST, &u64_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_U64_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(uint64_t), "U64 NVS_Get_Size_Of(): size == sizeof(uint64_t)");

	err = NVS_Get(NVS_U64_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Get() == ESP_OK");
	_nvsTestUInt64Equals(output, u64_test_high_extreme, "U64 NVS_Get(): output == input");

	err = NVS_Set(NVS_U64_TEST, &u64_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_U64_TEST, &u64_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_U64_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Get() == ESP_OK");
	_nvsTestUInt64Equals(output, u64_test_low_extreme, "U64 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_U64_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_U64_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "U64 NVS_Get() != ESP_OK After Erase");

}



 // ---------- NVS INT64 TESTS ----------
static void _testI64(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	int64_t output;
	int64_t i64_test_high_extreme = 9223372036854775807;
	int64_t i64_test_low_extreme = -9223372036854775808;

	err = NVS_Get(NVS_I64_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "I64 NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_I64_TEST, &i64_test_high_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "U64 NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_I64_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I64 NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(int64_t), "I64 NVS_Get_Size_Of(): size == sizeof(int64_t)");

	err = NVS_Get(NVS_I64_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I64 NVS_Get() == ESP_OK");
	_nvsTestInt64Equals(output, i64_test_high_extreme, "I64 NVS_Get(): output == input");

	err = NVS_Set(NVS_I64_TEST, &i64_test_high_extreme, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_I64_TEST, &i64_test_low_extreme, &size);
	_nvsTestInt32Equals(err, ESP_OK, "I64 NVS_Set() == ESP_OK");

	err = NVS_Get(NVS_I64_TEST, &output, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "I64 NVS_Get() == ESP_OK");
	_nvsTestInt64Equals(output, i64_test_low_extreme, "I64 NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_I64_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "I64 NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_I64_TEST, &output, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "I64 NVS_Get() != ESP_OK After Erase");

}



// ---------- NVS STRING TESTS ----------
static void _nvsTestStringEquals(char * testStr, const char * expectedStr, const char* testTitle){
	if(!strcmp(testStr, expectedStr)){
		configPRINTF(("[Test_str]PASSED %s, Expected Str: %s, Actual Str:%s\r\n", testTitle, expectedStr, testStr));
	}
	else{
		configPRINTF(("[Test_str]FAILED %s, Expected Str: %s, Actual Str:%s\r\n", testTitle, expectedStr, testStr));
	}
}

#define TEST_STR_1		"T"
#define TEST_STR_2		"Test1"
#define TEST_STR_LONG	"ThisTestIsForInputLargerThenOutput"

static void _testStr(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	char output[12];
	char input1[] = TEST_STR_1;
	char input2[] = TEST_STR_2;
	char inputLongerThanOutBuf[] = TEST_STR_LONG;

	err = NVS_Get(NVS_STR_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "Str NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_STR_TEST, input1, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_STR_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(input1), "Str NVS_Get_Size_Of(): size == sizeof(input)");

	err = NVS_Get(NVS_STR_TEST, &output, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(input1), "Str NVS_Get_Size_Of(): size == sizeof(input)");
	_nvsTestStringEquals(output, input1, "Str NVS_Get(): output == input");

	err = NVS_Set(NVS_STR_TEST, input1, &size);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_STR_TEST, input2, NULL);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_STR_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(input2), "Str NVS_Get_Size_Of(): size == sizeof(input)");

	err = NVS_Get(NVS_STR_TEST, &output, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(input2), "Str NVS_Get_Size_Of(): size == sizeof(input)");
	_nvsTestStringEquals(output, input2, "Str NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_STR_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_STR_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "Str NVS_Get() != ESP_OK After Erase");

	err = NVS_Set(NVS_STR_TEST, inputLongerThanOutBuf, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_STR_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, sizeof(inputLongerThanOutBuf), "Str NVS_Get_Size_Of(): size == sizeof(input)");

	// NOTES ON TEST BELOW:
	// THE RESULT WILL RETURN ESP_OK, but the string equal will fail
	// NOTE: CANNOT WRITE to output buffer that is smaller then return size.
	// Onus is on the caller to make sure that happens

//	err = NVS_Get(NVS_STR_TEST, &output, &size);
//	_nvsTestInt32Equals(err, ESP_OK, "Str NVS_Get() == ESP_OK");
//	_nvsTestInt32Equals(size, sizeof(inputLongerThanOutBuf), "Str NVS_Get(): size == sizeof(input)");
//	_nvsTestStringEquals(output, inputLongerThanOutBuf, "Str NVS_Get(): output == input");

}



// ---------- NVS BLOB TESTS ----------
static void _nvsTestBlobEquals(char * testBlob, const char * expectedBlob, uint32_t  blobSize, const char* testTitle){
	if(!memcmp(testBlob, expectedBlob, blobSize)){
		configPRINTF(("[Test_blob]PASSED %s, Expected Str: %.*s, Actual Str:%.*s, size:%u\r\n", testTitle, blobSize, expectedBlob, blobSize, testBlob, blobSize));
	}
	else{
		configPRINTF(("[Test_blob]FAILED %s, Expected Str: %.*s, Actual Str:%.*s, size:%u\r\n", testTitle, blobSize, expectedBlob, blobSize, testBlob, blobSize));
	}
}

#define TEST_BLOB_1		"T"
#define TEST_BLOB_2		"Test1"

static void _testBlob(void){
	// Ensure failure in both nvs get size of and nvs get if item does not exist in nvs yet
	esp_err_t err;
	uint32_t size;
	char output[12];
	char input1[] = TEST_BLOB_1;
	uint32_t inputSize = 1;
	char input2[] = TEST_BLOB_2;
	uint32_t inputSize2 = 5;

	err = NVS_Get(NVS_BLOB_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "Blob NVS_Get() != ESP_OK Before Being Set");

	err = NVS_Set(NVS_BLOB_TEST, input1, &inputSize);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_BLOB_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, inputSize, "Blob NVS_Get_Size_Of(): size == inputSize");

	err = NVS_Get(NVS_BLOB_TEST, &output, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(size, inputSize, "Blob NVS_Get_Size_Of(): size == inputSize");
	_nvsTestBlobEquals(output, input1, inputSize, "Blob NVS_Get(): output == input");

	err = NVS_Set(NVS_BLOB_TEST, input1, &inputSize);
	configPRINTF(("ENSURE A MESSAGE FROM NVS STATING THE NVS ITEM IS ALREADY IN NVS AND WONT BE SET\r\n"));

	err = NVS_Set(NVS_BLOB_TEST, input2, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "Blob NVS_Set() != ESP_OK if NULL pointer passed as size");

	err = NVS_Set(NVS_BLOB_TEST, input2, &inputSize2);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Set() == ESP_OK");

	err = NVS_Get_Size_Of(NVS_BLOB_TEST, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Get_Size_Of() == ESP_OK");
	_nvsTestInt32Equals(size, inputSize2, "Blob NVS_Get_Size_Of(): size == inputSize2");

	err = NVS_Get(NVS_BLOB_TEST, &output, &size);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_Get() == ESP_OK");
	_nvsTestInt32Equals(size, inputSize2, "Blob NVS_Get_Size_Of(): size == inputSize2");
	_nvsTestBlobEquals(output, input2, inputSize2, "Blob NVS_Get(): output == input");

	err = NVS_EraseKey(NVS_BLOB_TEST);
	_nvsTestInt32Equals(err, ESP_OK, "Blob NVS_EraseKey() == ESP_OK");

	err = NVS_Get(NVS_BLOB_TEST, &output, &size);
	_nvsTestInt32NotEquals(err, ESP_OK, "Blob NVS_Get() != ESP_OK After Erase");

}


#define OUT_OF_RANGE_INDEX	100
void _testItemNotInNVS(void){
	esp_err_t err;

	err = NVS_Get_Size_Of(OUT_OF_RANGE_INDEX, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "err != ESP_OK if out of index passed");

	err = NVS_Get(OUT_OF_RANGE_INDEX, NULL, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "err != ESP_OK if out of index passed");

	err = NVS_Set(OUT_OF_RANGE_INDEX, NULL, NULL);
	_nvsTestInt32NotEquals(err, ESP_OK, "err != ESP_OK if out of index passed");

	err = NVS_EraseKey(OUT_OF_RANGE_INDEX);
	_nvsTestInt32NotEquals(err, ESP_OK, "err != ESP_OK if out of index passed");
}

void runNVSTests(void){
	NVS_Initialize();

//	_testU8();
//	_testI8();
//	_testU16();
//	_testI16();
//	_testU32();
//	_testI32();
//	_testU64();
//	_testI64();
	_testStr();
//	_testBlob();
//	_testItemNotInNVS();
}



