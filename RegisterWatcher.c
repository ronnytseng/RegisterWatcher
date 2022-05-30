#include <form.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>


#define NUM_ROW      (32)
#define NUM_COL   (8)
#define FIELD_WIDTH  (12)
#define MSG_WIDTH (16)
#define TITLE_WIDTH (64)
#define MENU_ITEM_WIDTH (64)

#define MEMORY_FORM_STARTX  (16)
#define MEMORY_FORM_STARTY  (4)

#define MB (1024*1024)
#define KB (1024)

#define DEFAULT_KEY_ENTER ('\n')
#define DEFAULT_KEY_SEARCH ('g')
#define DEFAULT_KEY_NEXT_PAGE ('s')
#define DEFAULT_KEY_PREVIOUS_PAGE ('w')
#define DEFAULT_QUIT ('q')
#define DEFAULT_BACKSPACE (127)


typedef unsigned long long Uint64;      ///< Unsigned 64-bit integer
typedef unsigned int Uint32;            ///< Unsigned 32-bit integer
typedef unsigned short Uint16;          ///< Unsigned 16-bit integer
typedef unsigned char Uint8;            ///< Unsigned  8-bit integer


Uint32 gBasePhyStart;
Uint32 gSize;
Uint32 gPage=0;
Uint32 gIndex=0;
Uint32 reInitTitle=0;
Uint32 gExit=0;

FORM  *cell_form;
volatile Uint32 *initPressureMmapAddr = NULL;
Uint32 maxPage=0;
sigjmp_buf env;

char Msg1[]="Search for Address:";
char StrReserved[]="-Reserved-";


struct MemoryRegionInfo
{
	char name[128];
	Uint32 phyStart;
	Uint32 size;
} memoryRegInfo[] =
{
	{"GPMC",0x00000000,512*MB},
	{"PCIe",0x20000000,256*MB},
	{"ARM Cortex-A8 ROM",0x40020000,48*KB},
	{"ARM Cortex-A8 RAM",0x402F0000,64*KB},
	{"OCMC SRAM",0x40300000,128*KB},
	{"C674x L2 RAM",0x40800000,256*KB},
	{"C674x L1P Cache/RAM",0x40E00000,32*KB},
	{"C674x L1D Cache/RAM",0x40F00000,32*KB},
	{"L3 Fast configuration registers",0x44000000,4*MB},
	{"L3 Mid configuration registers",0x44400000,4*MB},
	{"L3 Slow configuration registers",0x44800000,4*MB},
	{"McASP0 Data Peripheral Registers",0x46000000,4*MB},
	{"McASP1 Data Peripheral Registers",0x46400000,4*MB},
	{"McASP2 Data Peripheral Registers",0x46800000,4*MB},
	{"HDMI",0x46C00000,4*MB},
	{"McBSP",0x47000000,4*MB},
	{"USB",0x47400000,4*MB},
	{"MMCSD2",0x47810000,8*KB},
	{"L4 Slow Peripheral Domain",0x48000000,16*MB},
	{"EDMA TPCC Registers",0x49000000,1*MB},
	{"EDMA TPTC0 Registers",0x49800000,1*MB},
	{"EDMA TPTC1 Registers",0x49900000,1*MB},
	{"EDMA TPTC2 Registers",0x49A00000,1*MB},
	{"EDMA TPTC3 Registers",0x49B00000,1*MB},
	{"L4 Fast Peripheral Domain",0x4A000000,16*MB},
	{"Emulation Subsystem",0x4B000000,16*MB},
	{"DDR0 Registers",0x4C000000,16*MB},
	{"DDR1 Registers",0x4D000000,16*MB},
	{"DDR DMM Registers",0x4E000000,32*MB},
	{"GPMC Registers",0x50000000,16*MB},
	{"PCIE Registers",0x51000000,16*MB},
	{"Media Controller",0x55000000,16*MB},
	{"SGX530",0x56000000,16*MB},
	{"HDVICP2 Configuration",0x58000000,16*MB},
	{"HDVICP SL2",0x59000000,16*MB},
	{"ISS",0x5C000000,32*MB},
	{"Search for?(0x????????)",0x0,0}
};






void hintMenu()
{
	//char buf[256];

	mvprintw(2,4, "[DM385/DM8127 register watcher]");
	mvprintw(4+(sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]))+1, 4, "[q:QUIT]");
	mvprintw(4+(sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]))+2, 4, "[arrow keys to move up or down the menu item]");
	mvprintw(4+(sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]))+3, 4, "[Enter: go to the target selected or start to input the address to search(in 'Search for' item)]");
}

void hintMemDump()
{

	mvprintw(MEMORY_FORM_STARTY+NUM_ROW+2+1, 0, "[q:Back to Menu; arrow keys to move the cursor; press Enter to write address (0x????)]");
	mvprintw(MEMORY_FORM_STARTY+NUM_ROW+2+2, 0, "[g:start to input the address to search (0x????????)]");
	mvprintw(MEMORY_FORM_STARTY+NUM_ROW+2+3, 0, "[s:Next address Page; w:Previous address page]");
}

void handle_sigbus(Uint32 sig)
{
	//printf("SIGBUS!\n");
	siglongjmp(env, 1);
}

Uint32 UnMapPhyAddr()
{
	munmap(initPressureMmapAddr, gSize);
	return 0;
}


Uint32 MapPhyAddr(Uint32 phyStart, Uint32 size)
{
	int memDevFd;

	gBasePhyStart = phyStart;
	gSize = size;

	memDevFd = open("/dev/mem",O_RDWR|O_SYNC);

	if(initPressureMmapAddr)
		UnMapPhyAddr();

	initPressureMmapAddr = mmap(
	                           NULL,
	                           //REG_INIT_PRESSURE_SIZE,
	                           size,
	                           PROT_READ|PROT_WRITE|PROT_EXEC,MAP_SHARED,
	                           memDevFd,
	                           //REG_INIT_PRESSURE_BASE_PHYS
	                           phyStart
	                       );


	maxPage = ((Uint32)(size)/(NUM_ROW*NUM_COL*4))-1;


	return 0;
}


int Edit_Field(FORM *f, Uint32* input)
{
	int ch;
	int length=0;
	char buf[16];
	char* psrc;
	FIELD *current;
	int start=2;

	current = current_field(f);
	set_field_buffer(current, 0, "0x");

	buf[0]='0';
	buf[1]='x';
	length=2;
	form_driver(f, REQ_END_FIELD);

	while((ch = getch())!= DEFAULT_KEY_ENTER)
	{
		if(ch == DEFAULT_BACKSPACE)
		{
			if(length > start) --length;
			buf[length] = 0;


			set_field_buffer(current, 0, buf);
			form_driver(f, REQ_END_FIELD);
		}
		else
		{
			if(length<10)
			{
				if((ch>= '0' && ch<='9') || (ch>='a' && ch<= 'f') || (ch>='A' && ch<= 'F'))
				{
					buf[length++] = ch;
					form_driver(f, ch);
				}
			}
		}
	}
	buf[length]=0;

	*input = strtoll(buf,&psrc,16);

	return 0;
}

int LocateFieldToSearchResult(Uint32 input)
{
	int i, shift;

	gPage = ((Uint32)input - (Uint32)memoryRegInfo[gIndex].phyStart)/(NUM_ROW*NUM_COL*4);
	shift = ((Uint32)input - (Uint32)memoryRegInfo[gIndex].phyStart)%(NUM_ROW*NUM_COL*4);
	shift/=4;

	form_driver(cell_form, REQ_FIRST_FIELD);

	for(i=0; i<shift; ++i)
		form_driver(cell_form, REQ_NEXT_FIELD);

	return 0;
}


int DisplayMenu(FORM *menu_form, FIELD *fieldMenu[])
{
	int ch,ch2,i,index;
	Uint32 input;
	int isFinished=0;
	int length, shift;
	int found=0;
	char buf[256];
	char* psrc;
	FIELD *current;
	erase();
	hintMenu();
	form_driver(cell_form, REQ_FIRST_FIELD);
	for(i=0; i<sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]) -1; ++i)
	{
		sprintf(buf,"0x%08x~0x%08x %s",memoryRegInfo[i].phyStart,memoryRegInfo[i].phyStart+memoryRegInfo[i].size-1,memoryRegInfo[i].name);
		set_field_buffer(fieldMenu[i], 0, buf);
	}
	set_field_buffer(fieldMenu[i], 0, memoryRegInfo[i].name);
	current = current_field(menu_form);
	set_field_back(current, A_STANDOUT);


	reInitTitle = 1;
	refresh();
	while(!isFinished)
	{
		ch = getch();
		switch(ch)
		{
			case KEY_DOWN:
				/* Go to next field */
				current = current_field(menu_form);
				set_field_back(current, A_NORMAL);
				form_driver(menu_form, REQ_NEXT_FIELD);

				current = current_field(menu_form);
				set_field_back(current, A_STANDOUT);
				/* Go to the end of the present buffer */
				/* Leaves nicely at the last character */
				form_driver(menu_form, REQ_BEG_LINE);
				break;
			case KEY_UP:
				/* Go to previous field */
				current = current_field(menu_form);
				set_field_back(current, A_NORMAL);
				form_driver(menu_form, REQ_PREV_FIELD);
				current = current_field(menu_form);
				set_field_back(current, A_STANDOUT);
				form_driver(menu_form, REQ_BEG_LINE);
				break;
			case DEFAULT_KEY_ENTER:
				current = current_field(menu_form);
				index = field_index(current);

				if(index == sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0])-1)
				{
					set_field_buffer(current, 0, "");
					Edit_Field(menu_form, &input);
					for(i=0; i<sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0])-1; ++i)
					{
						if(input>= memoryRegInfo[i].phyStart && (input<= (memoryRegInfo[i].phyStart+memoryRegInfo[i].size-1)))
						{
							found=1;
							break;
						}
					}

					if(!found)
					{
						isFinished=0;
						//index=0;
						set_field_buffer(fieldMenu[sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0])-1], 0, "Not in the range! Search for?(0x????????)");
					}
					else
					{
						//index=i;
						gIndex=i;

						LocateFieldToSearchResult(input);


						isFinished = 1;

					}

				}
				else
				{
					isFinished = 1;
					gPage=0;
					gIndex=index;
				}

				if(isFinished)
					MapPhyAddr(memoryRegInfo[gIndex].phyStart, memoryRegInfo[gIndex].size);

				break;
			case DEFAULT_QUIT:
				gExit=1;
				isFinished=1;
				break;
			default:
				break;
		}
	}
	erase();
	return 0;
}


int main()
{
	FORM *addr_form, *msg_form, *msg2_form, *menu_form;
	FIELD *field[NUM_ROW*NUM_COL+1];
	FIELD *fieldAddr[NUM_ROW+1];
	FIELD *fieldMsg[3];
	FIELD *fieldMsg2[3];
	FIELD *fieldMenu[(sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]))+1];

	FIELD *current;
	int ch, ch2;
	int i,j;

	int count=0;
	int total_offset=0;
	volatile Uint8* ptmp;
	char buf[256];
	char bufIn[50];
	char dbg[50];
	int index;
	Uint32 input;
	char* psrc;
	char *s;

	int length;
	int isJump=0;
	int shift=0;

	fd_set readfds;
	struct timeval tv;
	int select_retval;

	signal(SIGBUS, handle_sigbus);

	/* Initialize curses */
	initscr();
	cbreak();
	noecho();

	keypad(stdscr, TRUE);

	for(i=0; i<sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]); ++i)
	{
		fieldMenu[i] = new_field(1, MENU_ITEM_WIDTH, 4+i, 4, 0, 0);
	}
	fieldMenu[sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0])]=NULL;

	fieldMsg2[0]=new_field(1, TITLE_WIDTH, MEMORY_FORM_STARTY-2, 0, 0, 0);
	fieldMsg2[1]=new_field(1, TITLE_WIDTH, MEMORY_FORM_STARTY-3, 0, 0, 0);
	fieldMsg2[2]=NULL;

	menu_form = new_form(fieldMenu);
	post_form(menu_form);
	msg2_form = new_form(fieldMsg2);
	post_form(msg2_form);

	



	for(j=0; j<NUM_ROW; ++j)
		for(i=0; i<NUM_COL; ++i)
		{
			field[i+NUM_COL*j] = new_field(1, FIELD_WIDTH, MEMORY_FORM_STARTY+j, MEMORY_FORM_STARTX+i*FIELD_WIDTH, 0, 0);
			field_opts_off(field[i+NUM_COL*j], O_AUTOSKIP);  	/* Don't go to next field when this */
		}
	field[NUM_ROW*NUM_COL] = NULL;

	for(i=0; i<NUM_ROW; ++i)
	{
		fieldAddr[i] = new_field(1, FIELD_WIDTH, MEMORY_FORM_STARTY+i, 0, 0, 0);
		//field_opts_off(field[i+NUM_COL*j], O_AUTOSKIP);  	/* Don't go to next field when this */
	}
	fieldAddr[NUM_ROW] = NULL;

	fieldMsg[0]=new_field(1, sizeof(Msg1), MEMORY_FORM_STARTY+NUM_ROW+2, 0, 0, 0);
	fieldMsg[1]=new_field(1, MSG_WIDTH, MEMORY_FORM_STARTY+NUM_ROW+2, sizeof(Msg1), 0, 0);
	//fieldMsg[2]=new_field(1, 100, MEMORY_FORM_STARTY+NUM_ROW+2+2, 0, 0, 0);
	fieldMsg[2]=NULL;


	/* Create the form and post it */
	cell_form = new_form(field);
	post_form(cell_form);
	addr_form = new_form(fieldAddr);
	post_form(addr_form);
	msg_form = new_form(fieldMsg);
	post_form(msg_form);

	
	DisplayMenu(menu_form,fieldMenu);

	//set_field_buffer(fieldMsg[0], 0, "Go to Address:");
	//set_field_back(fieldMsg[1], A_UNDERLINE);

	refresh();
	

	while(!gExit)
	{

		//sprintf(dbg, "%d", count++);
		//mvprintw(40, 0, dbg);
		current = current_field(cell_form);
		//set_field_buffer(current, 0, "");
		index = field_index(current);
		sprintf(buf, "index: 0x%08x", (Uint32)gBasePhyStart + index*4 + gPage*NUM_COL*NUM_ROW*4);
		set_field_buffer(fieldMsg2[0], 0, buf);

		if(reInitTitle)
		{
			reInitTitle=0;
			sprintf(buf, "[%s] [range:  0x%08x ~ 0x%08x]", memoryRegInfo[gIndex].name, memoryRegInfo[gIndex].phyStart,memoryRegInfo[gIndex].phyStart+memoryRegInfo[gIndex].size-1);
			set_field_buffer(fieldMsg2[1], 0, buf);

			set_field_buffer(fieldMsg[0], 0, "Search for Address:");
			set_field_back(fieldMsg[1], A_UNDERLINE);
			hintMemDump();
			refresh();
		}
		for(j=0; j<NUM_ROW; ++j)
		{
			sprintf(buf, "0x%08x|", (Uint32)gBasePhyStart + j*NUM_COL*4 + gPage*NUM_COL*NUM_ROW*4);
			set_field_buffer(fieldAddr[j], 0, buf);

			for(i=0; i<NUM_COL; ++i)
			{
				memset(buf,0,sizeof(buf));
				if (!sigsetjmp(env, 1))
				{
					total_offset =  (i+j*NUM_COL)*4 +gPage*NUM_COL*NUM_ROW*4;

					if(total_offset > memoryRegInfo[gIndex].phyStart+memoryRegInfo[i].size)
						continue;

					ptmp= (Uint8*)initPressureMmapAddr + total_offset;
					sprintf(buf, "0x%08x", *((Uint32*)ptmp));
				}
				else
				{
					sprintf(buf, StrReserved);
					isJump = 1;
				}
				set_field_buffer(field[i+j*NUM_COL], 0, buf);
			}

		}

		refresh();

		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 1000000;

		select_retval = select(STDIN_FILENO+1, &readfds, NULL, NULL, &tv);
		if (select_retval > 0 || isJump)
		{
			isJump = 0;
			ch = getch();
			switch(ch)
			{

				case KEY_DOWN:
					/* Go to next field */
					for(i=0; i<NUM_COL; ++i)
						form_driver(cell_form, REQ_NEXT_FIELD);

					/* Go to the end of the present buffer */
					/* Leaves nicely at the last character */
					form_driver(cell_form, REQ_END_LINE);
					break;
				case KEY_UP:
					/* Go to previous field */
					for(i=0; i<NUM_COL; ++i)
						form_driver(cell_form, REQ_PREV_FIELD);
					form_driver(cell_form, REQ_END_LINE);
					break;

				case DEFAULT_KEY_NEXT_PAGE:
					if(gPage < maxPage)
						++gPage;
					else
						gPage=0;
					form_driver(cell_form, REQ_FIRST_FIELD);
					form_driver(cell_form, REQ_END_LINE);
					break;

				case DEFAULT_KEY_PREVIOUS_PAGE:
					if(gPage>0)
						--gPage;
					else
						gPage=maxPage;
					form_driver(cell_form, REQ_FIRST_FIELD);
					form_driver(cell_form, REQ_END_LINE);
					break;

				case DEFAULT_KEY_ENTER:
					
					current = current_field(cell_form);

					s = field_buffer(current, 0);

					for(i=0; i<strlen(StrReserved); ++i)
					{
						if(s[i]!=StrReserved[i])
							break;
					}
					if(i==strlen(StrReserved))
						break;

					set_field_buffer(current, 0, "");
					index = field_index(current);
					Edit_Field(cell_form, &input);
					total_offset =  (index)*4 +gPage*NUM_COL*NUM_ROW*4;

					ptmp= (Uint8*)initPressureMmapAddr + total_offset;
					*((volatile Uint32*)ptmp)=input;
					break;
				case DEFAULT_KEY_SEARCH:
					form_driver(msg_form, REQ_LAST_FIELD);

					Edit_Field(msg_form, &input);

					if((input< (Uint32)gBasePhyStart) || input > ((Uint32)gBasePhyStart + gSize))
					{
						set_field_buffer(fieldMsg[1], 0, "out of range!");
						break;
					}

					LocateFieldToSearchResult(input);

					//memset(dbg,0,sizeof(dbg));
					//sprintf(dbg, "0x%x", shift);
					//mvprintw(40, 0, dbg);

					break;
				case KEY_LEFT:
					/* Go to previous field */
					form_driver(cell_form, REQ_PREV_FIELD);

					form_driver(cell_form, REQ_END_LINE);
					break;

				case KEY_RIGHT:
					/* Go to previous field */
					form_driver(cell_form, REQ_NEXT_FIELD);

					form_driver(cell_form, REQ_END_LINE);
					break;

				case DEFAULT_QUIT:
					DisplayMenu(menu_form,fieldMenu);
					erase();
					break;
				default:
					/* If this is a normal character, it gets */
					/* Printed				  */

					//memset(dbg,0,sizeof(dbg));
					//sprintf(dbg, "0x%d", ch);
					//mvprintw(40, 0, dbg);

					//form_driver(cell_form, ch);
					break;
			}

		}
	}
	/* Un post form and free the memory */
	unpost_form(cell_form);
	free_form(cell_form);
	unpost_form(addr_form);
	free_form(addr_form);
	unpost_form(msg_form);
	free_form(msg_form);
	unpost_form(msg2_form);
	free_form(msg2_form);
	unpost_form(menu_form);
	free_form(menu_form);


	for(i=0; i<NUM_ROW*NUM_COL; ++i)
		free_field(field[i]);

	for(i=0; i<NUM_ROW+1; ++i)
		free_field(fieldAddr[i]);

	for(i=0; i<3; ++i)
		free_field(fieldMsg[i]);

	for(i=0; i<3; ++i)
		free_field(fieldMsg2[i]);

	for(i=0; i<(sizeof(memoryRegInfo)/sizeof(memoryRegInfo[0]))+1; ++i)
		free_field(fieldMenu[i]);

	endwin();
	return 0;
}
