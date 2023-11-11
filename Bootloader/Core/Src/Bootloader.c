/*
 * Name       : Bootloader.c
 * Created on : Oct 18, 2023
 * Author     : Shehab aldeen mohammed
 * Github     : https://github.com/ShehabAldeenMo
 * linkdin    : https://www.linkedin.com/in/shehab-aldeen-mohammed-0b3207228/
 */

/*============================ Includes  =============================*/
#include "Bootloader.h"

/*=============== Static Functions Declerations  =====================*/
static void Bootloader_Get_Version (uint8_t *Host_Buffer);
static void Bootloader_Get_Help (uint8_t *Host_Buffer);
static void Bootloader_Get_chip_Identification_Number (uint8_t *Host_Buffer);
static void Bootloader_Read_Protection_Level (uint8_t *Host_Buffer);
static void Bootloader_Jump_To_Address (uint8_t *Host_Buffer);
static void Bootloader_Erase_Flash (uint8_t *Host_Buffer);
static void Bootloader_Memory_Write (uint8_t *Host_Buffer);
static void Bootloader_Jump_To_User_App (uint8_t *Host_Buffer);

static CRC_Status Bootloader_CRC_Verify(uint8_t *pData , uint8_t Data_Len, uint32_t Host_CRC);
static void Bootloader_Send_ACK (uint8_t Reply_Length);
static void Bootloader_Send_NACK();
static void Bootloader_Send_Data_To_Host(uint8_t *Host_Buffer , uint32_t Data_Len);

static uint8_t CBL_STM32F103_GET_RDP_Level ();
static uint8_t Host_Jump_Address_Verfication (uint32_t Jump_Address);
static uint8_t Perform_Flash_Erase (uint32_t PageAddress, uint8_t Number_Of_Pages);
static uint8_t Flash_Memory_Write_Payload (uint8_t *Host_PayLoad , uint32_t Payload_Start_Address,uint8_t Payload_Len);

/*===================Static global Variables Definations  ==================*/
static uint8_t BL_HostBuffer[BL_HOST_BUFFER_RX_LENGTH];

/* All supported commends by bootloaders */
static uint8_t Bootloader_Supported_CMDs[NumberOfCommends] = {
	CBL_GET_VER_CMD ,
	CBL_GET_HELP_CMD ,
	CBL_GET_CID_CMD ,
	CBL_GET_RDP_STATUS_CMD,
	CBL_GO_TO_ADDER_CMD,
	CBL_FLASH_ERASE_CMD,
	CBL_MEM_WRITE_CMD,
	CBL_JUMP_TO_APP
};

static BL_pFunc Bootloader_Functions [NumberOfCommends] = {&Bootloader_Get_Version,
&Bootloader_Get_Help,&Bootloader_Get_chip_Identification_Number,&Bootloader_Read_Protection_Level,
&Bootloader_Jump_To_Address,&Bootloader_Erase_Flash,&Bootloader_Memory_Write
,&Bootloader_Jump_To_User_App} ;

/*======================== Software Interface Definations  ====================*/
BL_Status BL_UART_Fetch_Host_Commend(void) {
	/* To detect the status of function */
	BL_Status Status = BL_NACK;
	/* To dtect the status of uart in transmitting and receiving data */
	HAL_StatusTypeDef HAL_Status = HAL_ERROR;
	/* The data length that the host should be transmit at first */
	uint8_t Data_Length = 0;

	/* To clear buffer of RX and prevent carbadge messages of buffer */
	memset(BL_HostBuffer, 0, BL_HOST_BUFFER_RX_LENGTH);

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage("Bootloader started..\r\n");
#endif

	/* Host commend format :
	   => Commend Length  (1 byte = Data_Length )
	  */
	HAL_Status = HAL_UART_Receive(BL_HOST_COMMUNICATION_UART, BL_HostBuffer, 1,
			HAL_MAX_DELAY);

	if (HAL_Status != HAL_OK){
		Status = BL_NACK ;
	}
	else {
		/*
		 Depending on Data_Length we will recieve the number of bytes of the sending code
		 Commend Code (1 byte) + Delails (Data_Length) + CRC (4 bytes)
		 Where :
		 => Commend Code is the order that Host want to do in code
		 => Delails explain what you transmit
		 => CRC is safety algorthim on code
		 */
		Data_Length = BL_HostBuffer[0];

		/* we determine the number of recieving bytes next from the first number transmit in first
		   time (using buffer Data_Length and store them in BL_HostBuffer) */
		HAL_Status = HAL_UART_Receive(BL_HOST_COMMUNICATION_UART, &BL_HostBuffer[1],
				Data_Length, HAL_MAX_DELAY);

		/* if it don't recieve correctly */
		if (HAL_Status != HAL_OK){
			Status = BL_NACK ;
		}
		else {
			/* To jump on the target function from the previous commend */
			if (BL_HostBuffer[1]>=FIRST_COMMEND &&BL_HostBuffer[1]<=CBL_JUMP_TO_APP ){
				Bootloader_Functions[BL_HostBuffer[1]-FIRST_COMMEND](BL_HostBuffer);
			}
			else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
				BL_PrintMassage ("Invalid commend code recieved from host !! \r\n ");
#endif
				Status = BL_NACK ;
			}
		}
	}
	return Status;
}

static CRC_Status Bootloader_CRC_Verify(uint8_t *pData , uint8_t Data_Len, uint32_t Host_CRC){
	/* Detect CRC status */
	CRC_Status Status = CRC_NACK ;
	/* Buffering the calculations on host bytes that it transmitted */
	uint32_t MCU_CRC_Calculated = 0 ;
	/* Used as counter on the number of bytes that host transmit without CRC bytes */
	uint8_t Data_Counter = 0 ;
	/* To avoid warning in sending address of uint8_t variable in address of
	   uint32_t variable in HAL_CRC_Accumulate */
	uint32_t Data_Buffer = 0 ;

	/* Calculate CRC32 */
	for (Data_Counter = 0 ; Data_Counter < Data_Len ; Data_Counter++){
		Data_Buffer = (uint32_t)pData[Data_Counter];
		MCU_CRC_Calculated = HAL_CRC_Accumulate(CRC_ENGINE_OBJ, &Data_Buffer, 1);
	}

	/* Reset the CRC Calculations unit */
	__HAL_CRC_DR_RESET(CRC_ENGINE_OBJ);

	if (MCU_CRC_Calculated == Host_CRC ){
		Status = CRC_OK ;
	}
	else {
		Status = CRC_NACK ;
	}
	return Status ;
}

/* Send Acknowledge message of bootloader then the number of the bytes that you will transmit
   next to host */
static void Bootloader_Send_ACK (uint8_t Reply_Length){
	uint8_t ACK_Value[2] = {0};
	ACK_Value[0] = CBL_SEND_ACK;
	ACK_Value[1] = Reply_Length;
	Bootloader_Send_Data_To_Host(ACK_Value, 2);
}

/* Send Noacknowledge message */
static void Bootloader_Send_NACK(){
	uint8_t ACK_Value = CBL_SEND_NACK;
	Bootloader_Send_Data_To_Host(&ACK_Value, 1);
}

/* Function to communicate with host */
static void Bootloader_Send_Data_To_Host(uint8_t *Host_Buffer , uint32_t Data_Len){
	HAL_UART_Transmit(BL_HOST_COMMUNICATION_UART,(uint8_t*) Host_Buffer,(uint16_t) Data_Len, HAL_MAX_DELAY);
}

/*
 your packet is
   1- 1 byte for data length = 0x05
   2- 1 byte for commend number = 0x10
   3- 4 bytes for CRC verifications
   */
static void Bootloader_Get_Version (uint8_t *Host_Buffer){
	/* Buffering the version and vendor id's in BL_Version */
	uint8_t BL_Version[4] = { CBL_VENDOR_ID, CBL_SW_MAJOR_VERSION,
			CBL_SW_MINOR_VERSION, CBL_SW_PATCH_VERSION };
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0 ;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0 ;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Read bootloader version \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Sending Acknowledge message and number of bytes which will be sent */
		Bootloader_Send_ACK(4);

		/* Sending the version and vendor id's to meet the target from commend */
		Bootloader_Send_Data_To_Host(BL_Version,4);

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Bootloader Version %d.%d.%d\r\n",BL_Version[1],BL_Version[2],BL_Version[3]);
#endif
	}
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}

/*
 Your packet is :
   1- 1 byte data length = 0x05
   2- 1 byte commend number = 0x11
   3- 4 bytes for CRC verifications
	*/
static void Bootloader_Get_Help (uint8_t *Host_Buffer){
	uint16_t Host_CMD_Packet_Len = 0 ;  /* used to define the beginning of CRC address in buffer */
	uint32_t Host_CRC32 = 0 ;           /* Used to get CRC data */

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("All supported commends code \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Sending Acknowledge message and number of bytes which will be sent */
		Bootloader_Send_ACK(NumberOfCommends);
		/* Sending the list of commends to meet the target from commend */
		Bootloader_Send_Data_To_Host(Bootloader_Supported_CMDs,NumberOfCommends);
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage(
			"CBL_GET_VER_CMD 0x%x\r\nCBL_GET_HELP_CMD 0x%x\r\nCBL_GET_CID_CMD 0x%x\r\n",
			Bootloader_Supported_CMDs[0], Bootloader_Supported_CMDs[1],
			Bootloader_Supported_CMDs[2]);
	BL_PrintMassage(
			"CBL_GET_RDP_STATUS_CMD 0x%x\r\nCBL_GO_TO_ADDER_CMD 0x%x\r\nCBL_FLASH_ERASE_CMD 0x%x\r\n",
			Bootloader_Supported_CMDs[3], Bootloader_Supported_CMDs[4],
			Bootloader_Supported_CMDs[5]);
	BL_PrintMassage(
			"CBL_MEM_WRITE_CMD 0x%x\r\nCBL_EN_R_W_PROTECT_CMD 0x%x\r\nCBL_MEM_READ_CMD 0x%x\r\n",
			Bootloader_Supported_CMDs[6], Bootloader_Supported_CMDs[7],
			Bootloader_Supported_CMDs[8]);
	BL_PrintMassage(
			"CBL_READ_SECTOR_STATUS_CMD 0x%x\r\nCBL_OTP_READ_CMD 0x%x\r\nCBL_CHANGE_ROP_Level_CMD 0x%x\r\n",
			Bootloader_Supported_CMDs[9], Bootloader_Supported_CMDs[10],
			Bootloader_Supported_CMDs[11]);
	BL_PrintMassage("CBL_JUMP_TO_APP 0x%x\r\n",Bootloader_Supported_CMDs[12]);
#endif
	}
	else
		{
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}

/*
 Your packet is :
   1- 1 byte for data length = 0x05
   2- 1 byte for commend number = 0x12
   3- 4 bytes for CRC verifications
	*/
static void Bootloader_Get_chip_Identification_Number (uint8_t *Host_Buffer){
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0 ;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0 ;
	/* Identify the id of used MCU */
	uint16_t MCU_IdentificationNumber = 0 ;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Read MCU chip identification number \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Get MCU chip identification number */
		MCU_IdentificationNumber = (uint16_t)((DBGMCU->IDCODE)&0x00000FFF);

		/* Report MCU chip identification number */
		Bootloader_Send_ACK(2);
		Bootloader_Send_Data_To_Host((uint8_t *)(&MCU_IdentificationNumber),2);
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("IdentificationNumber = %x\r\n",MCU_IdentificationNumber);
#endif

	}
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}

static uint8_t CBL_STM32F103_GET_RDP_Level (){
	/* paramter input for function that get level of memory */
	FLASH_OBProgramInitTypeDef FLASH_OBProgram ;
	/* Get level of memory */
	HAL_FLASHEx_OBGetConfig(&FLASH_OBProgram);
	/* Assign protection level in parameter [in\out] */
	return (uint8_t)FLASH_OBProgram.RDPLevel ;
}

/*
 Your packet is :
   1- 1 byte data length = 0x05
   2- 1 byte commend number = 0x13
   3- 4 bytes for CRC verifications
 */
static void Bootloader_Read_Protection_Level (uint8_t *Host_Buffer){
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0 ;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0 ;
	/* Level of protection */
	uint8_t RDP_Level = 0 ;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Read the flash protection out level \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Report acknowledge message*/
		Bootloader_Send_ACK(1);
		/* Read protection level */
		RDP_Level = CBL_STM32F103_GET_RDP_Level();
		/* Report level */
		Bootloader_Send_Data_To_Host((uint8_t *)(&RDP_Level),1);
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Protection level = %x\r\n",RDP_Level);
#endif
	}
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}


static uint8_t Host_Jump_Address_Verfication (uint32_t Jump_Address){
	/* TO check on state of given address is in region or not */
	uint8_t Address_Verification_State = ADDRESS_IS_INVALID ;
	if (Jump_Address>= SRAM_BASE && Jump_Address <=STM32F103_SRAM_END){
		Address_Verification_State = ADDRESS_IS_VALID ;
	}
	else if(Jump_Address>= FLASH_BASE && Jump_Address <=STM32F103_FLASH_END){
		Address_Verification_State = ADDRESS_IS_VALID ;
	}
	else {
		Address_Verification_State = ADDRESS_IS_INVALID ;
	}
	return Address_Verification_State ;
}

/*
 Your packet is :
   1- 1 byte data length = 0x09
   2- 1 byte commend number = 0x14
   3- 4 bytes for address
   4- 4 bytes for CRC verifications
	*/
static void Bootloader_Jump_To_Address (uint8_t *Host_Buffer){
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0 ;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0 ;
	/* Buffering address */
	uint32_t Host_Jump_Address = 0 ;
	/* TO check on state of given address is in region or not */
	uint8_t Address_Verification_State = ADDRESS_IS_INVALID ;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Bootloader jump to specified address \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Report acknowledge message */
		Bootloader_Send_ACK(1);

		/* To get the content of this address */
		Host_Jump_Address = *((uint32_t *) &(Host_Buffer[2])) ;

		/* To verify that the address in the region of memory */
		Address_Verification_State = Host_Jump_Address_Verfication(Host_Jump_Address);

		if (Address_Verification_State == ADDRESS_IS_VALID ){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Address verification sucessed\r\n");
#endif
		Bootloader_Send_Data_To_Host((uint8_t *)&Address_Verification_State, 1);

		if (Host_Jump_Address == FLASH_PAGE_BASE_ADDRESS){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Jump To Application\r\n");
#endif
			Bootloader_Jump_To_User_App(Host_Buffer);
		}
		else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Jumped to : 0x%X \r\n",Host_Jump_Address);
#endif
			/* - Prepare the address to jump
				   - Increment 1 to be in thumb instruction */
				Jump_Ptr Jump_Address = (Jump_Ptr) (Host_Jump_Address + 1) ;
				Jump_Address();
		}
	}
		else {
			Bootloader_Send_Data_To_Host((uint8_t *)&Address_Verification_State, 1);
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Address verification unsucessed\r\n");
#endif
		}
	}
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}

/*
 Your packet is :
   1- 1 byte data length = 0x01
   2- 1 byte commend number = 0x17
 And be sure that
   1- base address in application is updated in (Bootloader_Jump_To_User_App)
   2- update size of bootloader code with suitable size as 17k or 15k
   3- update origin address of application code in flash memory in linker script and size also
   4- update the interrupt vector table to be allocate at start address of code in file (system_stm32f1xx.c)
       SCB->VTOR = FLASH_BASE | 0x8000;
  */
static void Bootloader_Jump_To_User_App (uint8_t *Host_Buffer){
	/* Value of the main stack pointer of our main application */
	uint32_t MSP_Value = *((volatile uint32_t*)FLASH_PAGE_BASE_ADDRESS);
	/* Reset Handler defination function of our main application */
	uint32_t MainAppAddr = *((volatile uint32_t*)(FLASH_PAGE_BASE_ADDRESS+4));

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Jump to application\r\n");
#endif

	/* Declare pointer to function contain the beginning address of application */
	pFunc ResetHandler_Address = (pFunc)MainAppAddr;

	/* Deinitionalization of modules that used in bootloader and work
	   the configurations of new application */
	HAL_RCC_DeInit(); /* Resets the RCC clock configuration to the default reset state. */

	/* Reset main stack pointer */
	__set_MSP(MSP_Value);

	/* Jump to Apllication Reset Handler */
	ResetHandler_Address();
}

static uint8_t Perform_Flash_Erase (uint32_t PageAddress, uint8_t Number_Of_Pages){
	/* To check that the sectors in not overflow the size of flash */
	uint8_t Page_validity_Status  = PAGE_INVALID_NUMBER ;
	/* Status of erasing flash */
	HAL_StatusTypeDef HAL_Status = HAL_ERROR ;
	/* Error sector status */
	uint32_t PageError = 0 ;
	/* Define struct to configure parameters[in] */
	FLASH_EraseInitTypeDef pEraseInit ;
	/* Define the used bank in flash memory */
	pEraseInit.Banks = FLASH_BANK_1 ;

	/* another pages is agreed but check that is acess the number of pages in flash */
	if (Number_Of_Pages >= CBL_FLASH_MAX_PAGES_NUMBER
			&& CBL_FLASH_MASS_ERASE != PageAddress) {
		Page_validity_Status = PAGE_INVALID_NUMBER ;
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage ("It is over the flash size\r\n");
#endif
	}
	/* erase all memory or specific page */
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("It is in range of flash memory \r\n");
#endif
		Page_validity_Status = PAGE_VALID_NUMBER ;

		/* Check if he want to erase all memory flash */
		if ( CBL_FLASH_MASS_ERASE == PageAddress  ){
			pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
			pEraseInit.PageAddress = FLASH_PAGE_BASE_ADDRESS;
			pEraseInit.NbPages = APPLICATION_SIZE;
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Mase erase \r\n");
#endif
		}
		/* erase specific page */
		else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Page erase \r\n");
#endif
			pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
			pEraseInit.PageAddress = PageAddress;
			pEraseInit.NbPages = Number_Of_Pages;
		}

		/* To unlock flash memory */
		HAL_Status = HAL_FLASH_Unlock();

		/* if it's opened correctly */
		if (HAL_Status == HAL_OK){
			/* Perform a mass erase or erase the specified FLASH memory sectors */
			HAL_Status = HAL_FLASHEx_Erase(&pEraseInit, &PageError);

			/* To check that the flash memory is erased sucessfully */
			if (HAL_SUCESSFUL_ERASE == PageError){
				Page_validity_Status = SUCESSFUL_ERASE ;
			}
			/* didn't erase*/
			else {
				Page_validity_Status = UNSUCESSFUL_ERASE ;
			}

			HAL_Status = HAL_FLASH_Lock();
		}
		/* Not opened */
		else {
			Page_validity_Status = UNSUCESSFUL_ERASE ;
		}
	}
	return Page_validity_Status ;
}

/*
 Your packet is :
   1- 1 byte data length = 0x07
   2- 1 byte commend number = 0x11
   3- 4 bytes for page address
   4- 1 byte for number of pages
   5- 4 bytes for CRC verifications
	*/
static void Bootloader_Erase_Flash (uint8_t *Host_Buffer){
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0 ;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0 ;
	/* To check on Erase state */
	uint8_t Erase_Status = UNSUCESSFUL_ERASE ;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Mase erase or page erase of the user flash \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verfications */
	if ( CRC_OK == Bootloader_CRC_Verify(Host_Buffer, (Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32)){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif
		/* Send acknowledge to host */
		Bootloader_Send_ACK(1);
		/* Perform Mass erase or sector erase of the yser flash */
		Erase_Status = Perform_Flash_Erase ( *( (uint32_t*)&Host_Buffer[2] ),Host_Buffer[6]);
		/* Report the erase state */
		Bootloader_Send_Data_To_Host((uint8_t *)(&Erase_Status),1);
		if ( SUCESSFUL_ERASE == Erase_Status){
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Sucessful erased\r\n");
#endif
		}
		else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("Unsucessful erased\r\n");
#endif
		}
	}
	else {
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		Bootloader_Send_NACK();
	}
}

static uint8_t Flash_Memory_Write_Payload(uint8_t *Host_PayLoad,
	uint32_t Payload_Start_Address, uint8_t Payload_Len) {
	/* The status in dealing HAL functions */
	HAL_StatusTypeDef HAL_Status = HAL_ERROR;
	/* Status writing in flash memory */
	uint8_t Status = FLASH_PAYLOAD_WRITING_FAILED;
	/* The number of words in data appliction sections */
	uint8_t PayLoad_Counter = 0 ;
	/* buffering half word */
	uint16_t Payload_Buffer  = 0 ;
	/* address of current writing half word */
	uint32_t Address         = 0 ;

	/* Writing steps */
	/* Open flash memory */
	HAL_Status = HAL_FLASH_Unlock();

	/* If it opened */
	if (HAL_Status == HAL_OK) {
		/* Transfer the data sections half word by half word */
		while (Payload_Len !=0 && Status == HAL_OK ){
			Payload_Buffer = (uint16_t) Host_PayLoad[PayLoad_Counter]
				|( (uint16_t)Host_PayLoad[PayLoad_Counter+1] << TWO_BYTES );

			/*update the flash address each iltration */
			Address = Payload_Start_Address + PayLoad_Counter ;

			/*Writing the Date in the flash Halfword by Halfword */
			HAL_Status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
					Address, (uint64_t)Payload_Buffer);

			/*To increment PayLoad_Counter and stopped on new half word in host packet frame
			  To decrement Payload_Len to be sure that all of host packet frame is written
			  */
			Payload_Len-=2;
			PayLoad_Counter+=2;
		}

		/* if function can't write on memory Status be failed */
		if (HAL_Status != HAL_OK) {
			Status = FLASH_PAYLOAD_WRITING_FAILED;
		} else {
			/* All iterations, It can write on memory make status passed */
			Status = FLASH_PAYLOAD_WRITING_PASSED;
		}
	}
	else {
		/* If it can't open memory make status failed */
		Status = FLASH_PAYLOAD_WRITING_FAILED;
	}

	/* If all status is OK so It will lock memory */
	if (Status == FLASH_PAYLOAD_WRITING_PASSED && HAL_Status == HAL_OK) {
		HAL_Status = HAL_FLASH_Lock();

		/* Check if it locked it true or not */
		if (HAL_Status != HAL_OK) {
			Status = FLASH_PAYLOAD_WRITING_FAILED;
		} else {
			Status = FLASH_PAYLOAD_WRITING_PASSED;
		}
	} else {
		/* If one of status is not OK so It will make returned status with failed */
		Status = FLASH_PAYLOAD_WRITING_FAILED;
	}
	return Status;
}

/*
 Your packet is :
   1- 1 byte data length = 0x0B+0x0N
   2- 1 byte commend number = 0x16
   3- 4 bytes for address
   4- 1 byte for size of writing data
   5- N bytes of data info
   6- 4 bytes for CRC verifications
	*/
static void Bootloader_Memory_Write (uint8_t *Host_Buffer){
	/* used to define the beginning of CRC address in buffer */
	uint16_t Host_CMD_Packet_Len = 0;
	/* Used to get CRC data */
	uint32_t Host_CRC32 = 0;
	/* Base address that you will write on */
	uint32_t HOST_Address = 0;
	/* Number of bytes that will be sent */
	uint8_t Payload_Len = 0;
	/* The status of input address from the host */
	uint8_t Address_Verification = ADDRESS_IS_INVALID;
	/* Status writing in flash memory */
	uint8_t Flash_Payload_Write_Status = FLASH_PAYLOAD_WRITING_FAILED;

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
	BL_PrintMassage ("Write data into different memory \r\n");
#endif

	/* Extract the CRC32 and Packet length sent by the HOST */
	Host_CMD_Packet_Len = Host_Buffer[0]+1 ;
	Host_CRC32 = *(uint32_t *)(Host_Buffer + Host_CMD_Packet_Len - CRC_TYPE_SIZE) ;

	/* CRC Verification */
	if(CRC_OK == Bootloader_CRC_Verify(Host_Buffer,(Host_CMD_Packet_Len - CRC_TYPE_SIZE), Host_CRC32))
	{

#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is passed\r\n");
#endif

		/* Send acknowledgement to the HOST */
		Bootloader_Send_ACK(1);

		/* Extract the start address from the Host packet */
		HOST_Address = *((uint32_t *)(&Host_Buffer[2]));

		/* Extract the payload length from the Host packet */
		Payload_Len = Host_Buffer[6];

		/* Verify the Extracted address to be valid address */
		Address_Verification = Host_Jump_Address_Verfication(HOST_Address);

		if(ADDRESS_IS_VALID == Address_Verification)
		{
			/* Write the payload to the Flash memory */
			Flash_Payload_Write_Status = Flash_Memory_Write_Payload((uint8_t *)&Host_Buffer[7], HOST_Address, Payload_Len);

			/* Report payload writing state */
			Bootloader_Send_Data_To_Host((uint8_t *)&Flash_Payload_Write_Status, 1);
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
			BL_PrintMassage ("Correct writing data into memory at %x \r\n",HOST_Address);
#endif
		}
		else
		{
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
			BL_PrintMassage ("Incorrect writing data into memory at %x \r\n",HOST_Address);
#endif
			/* Report address verification failed */
			Address_Verification = ADDRESS_IS_INVALID;
			Bootloader_Send_Data_To_Host((uint8_t *)&Address_Verification, 1);
		}
	}
	else
	{
#if BL_DEBUG_ENABLE == DEBUG_INFO_ENABLE
		BL_PrintMassage("CRC is failed\r\n");
#endif
		/* Send Not acknowledge to the HOST */
		Bootloader_Send_NACK();
	}
}


void BL_PrintMassage(char *format, ...) {
	char Message[100] = { 0 };
	va_list args;
	/* Enable acess to the variable arguments */
	va_start(args, format);
	/* Write the formatted data from variable argument list to string */
	vsprintf(Message, format, args);
#if  BL_DEBUG_METHOD == BL_ENABLE_UART_DEBUG_MESSAGE
	/* Transmit the formatted data through the defined UART */
	HAL_UART_Transmit(BL_DEBUG_UART, (uint8_t*) Message, (uint16_t)sizeof(Message),
	HAL_MAX_DELAY);
#elif  BL_DEBUG_METHOD == BL_ENABLE_CAN_DEBUG_MESSAGE
	/* Transmit the formatted data through the defined CAN */
#elif  BL_DEBUG_METHOD == BL_ENABLE_ETHERNET_DEBUG_MESSAGE
	/* Transmit the formatted data through the defined ETHERNET */
#endif
	/* Perform cleanup for an ap object initialized by a call to va_start */
	va_end(args);
}

