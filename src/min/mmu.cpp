// GB Enhanced+ Copyright Daniel Baxter 2020
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : mmu.cpp
// Date : December 01, 2020
// Description : Pokemon Mini memory manager unit
//
// Handles reading and writing bytes to memory locations
// Also loads ROM and BIOS files

#include "mmu.h"

/****** MMU Constructor ******/
MIN_MMU::MIN_MMU() 
{
	reset();
	init_ir();
}

/****** MMU Deconstructor ******/
MIN_MMU::~MIN_MMU() 
{
	if(save_eeprom) { save_backup(config::save_file); }
	memory_map.clear();
	disconnect_ir();
	std::cout<<"MMU::Shutdown\n";
}

/****** MMU Reset ******/
void MIN_MMU::reset()
{
	memory_map.clear();
	memory_map.resize(0x200000, 0x00);

	//Trigger System Reset IRQ
	master_irq_flags = 0x1;

	//Setup and enable NMIs
	irq_enable[0] = true;
	irq_enable[1] = true;
	irq_enable[2] = true;

	irq_priority[0] = 4;
	irq_priority[1] = 4;
	irq_priority[2] = 4;

	for(u32 x = 3; x < 32; x++)
	{
		irq_enable[x] = false;
		irq_priority[x] = 0;
	}

	osc_1_enable = false;
	osc_2_enable = false;

	eeprom.data.clear();
	eeprom.data.resize(0x2000, 0xFF);

	eeprom.clk = 0;
	eeprom.last_clk = 0;
	eeprom.state = 0;
	eeprom.sda = 0;
	eeprom.last_sda = 0;
	eeprom.addr = 0;
	eeprom.addr_bit = 0;
	eeprom.control = 0;

	eeprom.read_mode = false;
	eeprom.clock_data = false;
	eeprom.start_cond = false;
	eeprom.stop_cond = false;

	save_eeprom = false;

	//Advanced debugging
	#ifdef GBE_DEBUG
	debug_write = false;
	debug_read = true;
	debug_addr = 0;
	#endif

	timer = NULL;
}

/****** Read byte from memory ******/
u8 MIN_MMU::read_u8(u32 address) 
{
	//Advanced debugging
	#ifdef GBE_DEBUG
	debug_read = true;
	debug_addr = address;
	#endif

	//Mirror Cart ROM
	if(address >= 0x200000) { address &= 0x1FFFFF; }

	//Process MMIO registers
	switch(address)
	{
		case TIMER1_COUNT_LO:
			return timer->at(0).counter & 0xFF;

		case TIMER1_COUNT_HI:
			return (timer->at(0).counter >> 8);

		case TIMER2_COUNT_LO:
			return timer->at(1).counter & 0xFF;

		case TIMER2_COUNT_HI:
			return (timer->at(1).counter >> 8);

		case TIMER3_COUNT_LO:
			return timer->at(2).counter & 0xFF;

		case TIMER3_COUNT_HI:
			return (timer->at(2).counter >> 8);

		case TIMER256_CNT:
			return timer->at(3).cnt;

		case TIMER256_COUNT:
			return timer->at(3).counter;

		case PM_KEYPAD:
			return g_pad->key_input;

		case PRC_RATE:
			return lcd_stat->prc_rate;

		case PRC_CNT:
			return lcd_stat->prc_counter;
	}

	return memory_map[address];
}

/****** Reads signed byte from memory ******/
s8 MIN_MMU::read_s8(u32 address)
{
	u8 mem_byte = read_u8(address);

	//Convert to 2's complement
	if(mem_byte & 0x80)
	{
		mem_byte -= 1;
		mem_byte = ~mem_byte;

		return (mem_byte * -1);
	}

	return mem_byte;
}

/****** Reads signed 16-bits from memory ******/
s16 MIN_MMU::read_s16(u32 address)
{
	u16 mem_byte = read_u16(address);

	//Convert to 2's complement
	if(mem_byte & 0x8000)
	{
		mem_byte -= 1;
		mem_byte = ~mem_byte;

		return (mem_byte * -1);
	}

	return mem_byte;
}

/****** Read 16-bits from memory ******/
u16 MIN_MMU::read_u16(u32 address)
{
	return ((read_u8(address+1) << 8) | read_u8(address));
}

/****** Write byte to memory ******/
void MIN_MMU::write_u8(u32 address, u8 value)
{
	//Advanced debugging
	#ifdef GBE_DEBUG
	debug_write = true;
	debug_addr = address;
	#endif

	//Only write to RAM and MMIO registers
	if((address > 0xFFF)  && (address < 0x2100)) { memory_map[address] = value; }

	//Process MMIO registers
	switch(address)
	{
		//Interrupt Priority 1
		case IRQ_PRI_1:
			irq_priority[3] = (value >> 6) & 0x3;
			irq_priority[4] = (value >> 6) & 0x3;

			irq_priority[5] = (value >> 4) & 0x3;
			irq_priority[6] = (value >> 4) & 0x3;

			irq_priority[7] = (value >> 2) & 0x3;
			irq_priority[8] = (value >> 2) & 0x3;

			irq_priority[9] = (value & 0x3);
			irq_priority[10] = (value & 0x3);

			break;

		//Interrupt Priority 2
		case IRQ_PRI_2:
			irq_priority[11] = (value >> 6) & 0x3;
			irq_priority[12] = (value >> 6) & 0x3;
			irq_priority[13] = (value >> 6) & 0x3;
			irq_priority[14] = (value >> 6) & 0x3;

			irq_priority[19] = (value >> 4) & 0x3;
			irq_priority[20] = (value >> 4) & 0x3;

			irq_priority[21] = (value >> 2) & 0x3;
			irq_priority[22] = (value >> 2) & 0x3;
			irq_priority[23] = (value >> 2) & 0x3;
			irq_priority[24] = (value >> 2) & 0x3;
			irq_priority[25] = (value >> 2) & 0x3;
			irq_priority[26] = (value >> 2) & 0x3;
			irq_priority[27] = (value >> 2) & 0x3;
			irq_priority[28] = (value >> 2) & 0x3;

			irq_priority[29] = (value & 0x3);
			irq_priority[30] = (value & 0x3);
			irq_priority[31] = (value & 0x3);

			break;

		//Interrupt Priority 3
		case IRQ_PRI_3:
			irq_priority[15] = (value & 0x3);
			irq_priority[16] = (value & 0x3);

			break;

		//Interrupt Enable 1
		case IRQ_ENA_1:
			irq_enable[3] = (value & 0x80);
			irq_enable[4] = (value & 0x40);
			irq_enable[5] = (value & 0x20);
			irq_enable[6] = (value & 0x10);
			irq_enable[7] = (value & 0x08);
			irq_enable[8] = (value & 0x04);
			irq_enable[9] = (value & 0x02);
			irq_enable[10] = (value & 0x01);
			break;

		//Interrupt Enable 2
		case IRQ_ENA_2:
			irq_enable[11] = (value & 0x20);
			irq_enable[12] = (value & 0x10);
			irq_enable[13] = (value & 0x08);
			irq_enable[14] = (value & 0x04);
			irq_enable[19] = (value & 0x02);
			irq_enable[20] = (value & 0x01);
			break;

		//Interrupt Enable 3
		case IRQ_ENA_3:
			irq_enable[21] = (value & 0x80);
			irq_enable[22] = (value & 0x40);
			irq_enable[23] = (value & 0x20);
			irq_enable[24] = (value & 0x10);
			irq_enable[25] = (value & 0x08);
			irq_enable[26] = (value & 0x04);
			irq_enable[27] = (value & 0x02);
			irq_enable[28] = (value & 0x01);
			break;

		//Interrupt Enable 4
		case IRQ_ENA_4:
			irq_enable[15] = (value & 0x80);
			irq_enable[16] = (value & 0x40);
			irq_enable[29] = (value & 0x04);
			irq_enable[30] = (value & 0x02);
			irq_enable[31] = (value & 0x01);
			break;

		//Interrupt Active 1
		case IRQ_ACT_1:
			master_irq_flags &= (value & 0x80) ? ~0x08 : 0;
			master_irq_flags &= (value & 0x40) ? ~0x10 : 0;
			master_irq_flags &= (value & 0x20) ? ~0x20 : 0;
			master_irq_flags &= (value & 0x10) ? ~0x40 : 0;
			master_irq_flags &= (value & 0x08) ? ~0x80 : 0;
			master_irq_flags &= (value & 0x04) ? ~0x100 : 0;
			master_irq_flags &= (value & 0x02) ? ~0x200 : 0;
			master_irq_flags &= (value & 0x01) ? ~0x400 : 0;

			memory_map[IRQ_ACT_1] = memory_map[IRQ_ACT_1] & ~value;

			break;

		//Interrupt Active 2
		case IRQ_ACT_2:
			master_irq_flags &= (value & 0x20) ? ~0x800 : 0;
			master_irq_flags &= (value & 0x10) ? ~0x1000 : 0;
			master_irq_flags &= (value & 0x08) ? ~0x2000 : 0;
			master_irq_flags &= (value & 0x04) ? ~0x4000 : 0;
			master_irq_flags &= (value & 0x02) ? ~0x80000 : 0;
			master_irq_flags &= (value & 0x01) ? ~0x100000 : 0;

			memory_map[IRQ_ACT_2] = memory_map[IRQ_ACT_2] & ~value;

			break;

		//Interrupt Active 3
		case IRQ_ACT_3:
			master_irq_flags &= (value & 0x80) ? ~0x200000 : 0;
			master_irq_flags &= (value & 0x40) ? ~0x400000 : 0;
			master_irq_flags &= (value & 0x20) ? ~0x800000 : 0;
			master_irq_flags &= (value & 0x10) ? ~0x1000000 : 0;
			master_irq_flags &= (value & 0x08) ? ~0x2000000 : 0;
			master_irq_flags &= (value & 0x04) ? ~0x4000000 : 0;
			master_irq_flags &= (value & 0x02) ? ~0x8000000 : 0;
			master_irq_flags &= (value & 0x01) ? ~0x10000000 : 0;

			memory_map[IRQ_ACT_3] = memory_map[IRQ_ACT_3] & ~value;

			break;

		//Interrupt Active 4
		case IRQ_ACT_4:
			master_irq_flags &= (value & 0x80) ? ~0x8000 : 0;
			master_irq_flags &= (value & 0x40) ? ~0x10000 : 0;
			master_irq_flags &= (value & 0x04) ? ~0x20000000 : 0;
			master_irq_flags &= (value & 0x02) ? ~0x40000000 : 0;
			master_irq_flags &= (value & 0x01) ? ~0x80000000 : 0;

			memory_map[IRQ_ACT_4] = memory_map[IRQ_ACT_4] & ~value;

			break;

		//Timer 1 Prescalar
		case TIMER1_SCALE:
			//Update Prescalar
			update_prescalar(0);

			//Enable low and hi scalars
			timer->at(0).enable_scalar_lo = (value & 0x08) ? true : false;
			timer->at(0).enable_scalar_hi = (value & 0x80) ? true : false;

			break;

		//Timer 1 Oscillator
		case TIMER1_OSC:
			timer->at(0).osc_lo = (value & 0x1) ? 1 : 0;
			timer->at(0).osc_hi = (value & 0x2) ? 1 : 0;

			//Enable Oscillators 1 or 2
			osc_1_enable = (value & 0x20) ? true : false;
			osc_2_enable = (value & 0x10) ? true : false;

			//Update Prescalar
			update_prescalar(0);

			break;

		//Timer 2 Prescalar
		case TIMER2_SCALE:
			//Update Prescalar
			update_prescalar(1);

			//Enable low and hi scalars
			timer->at(1).enable_scalar_lo = (value & 0x08) ? true : false;
			timer->at(1).enable_scalar_hi = (value & 0x80) ? true : false;

			break;

		//Timer 2 Oscillator
		case TIMER2_OSC:
			timer->at(1).osc_lo = (value & 0x1) ? 1 : 0;
			timer->at(1).osc_hi = (value & 0x2) ? 1 : 0;

			//Update Prescalar
			update_prescalar(1);

			break;

		//Timer 3 Prescalar
		case TIMER3_SCALE:
			//Update Prescalar
			update_prescalar(2);

			//Enable low and hi scalars
			timer->at(2).enable_scalar_lo = (value & 0x08) ? true : false;
			timer->at(2).enable_scalar_hi = (value & 0x80) ? true : false;

			break;

		//Timer 3 Oscillator
		case TIMER3_OSC:
			timer->at(2).osc_lo = (value & 0x1) ? 1 : 0;
			timer->at(2).osc_hi = (value & 0x2) ? 1 : 0;

			//Update Prescalar
			update_prescalar(2);

			break;

		//Timer 1 Control
		case TIMER1_CNT_LO:
		case TIMER1_CNT_HI:
			timer->at(0).cnt = (memory_map[TIMER1_CNT_HI] << 8) | memory_map[TIMER1_CNT_LO];
			timer->at(0).full_mode = (timer->at(0).cnt & 0x80) ? true : false;			

			//High and Low enable
			timer->at(0).enable_hi = timer->at(0).cnt & 0x400;
			timer->at(0).enable_lo = timer->at(0).cnt & 0x4;

			//High and Low reset
			if(timer->at(0).cnt & 0x200) { timer->at(0).counter &= (timer->at(0).full_mode) ? 0x00 : 0xFF; timer->at(0).clock_hi = 0; }
			if(timer->at(0).cnt & 0x2) { timer->at(0).counter &= 0xFF00; timer->at(0).clock_lo = 0; }

			break;

		//Timer 2 Control
		case TIMER2_CNT_LO:
		case TIMER2_CNT_HI:
			timer->at(1).cnt = (memory_map[TIMER2_CNT_HI] << 8) | memory_map[TIMER2_CNT_LO];
			timer->at(1).full_mode = (timer->at(1).cnt & 0x80) ? true : false;			

			//High and Low enable
			timer->at(1).enable_hi = timer->at(1).cnt & 0x400;
			timer->at(1).enable_lo = timer->at(1).cnt & 0x4;

			//High and Low reset
			if(timer->at(1).cnt & 0x200) { timer->at(1).counter &= (timer->at(1).full_mode) ? 0x00 : 0xFF; timer->at(1).clock_hi = 0; }
			if(timer->at(1).cnt & 0x2) { timer->at(1).counter &= 0xFF00; timer->at(1).clock_lo = 0; }

			break;

		//Timer 3 Control
		case TIMER3_CNT_LO:
		case TIMER3_CNT_HI:
			timer->at(2).cnt = (memory_map[TIMER3_CNT_HI] << 8) | memory_map[TIMER3_CNT_LO];
			timer->at(2).full_mode = (timer->at(2).cnt & 0x80) ? true : false;			

			//High and Low enable
			timer->at(2).enable_hi = timer->at(2).cnt & 0x400;
			timer->at(2).enable_lo = timer->at(2).cnt & 0x4;

			apu_stat->sound_on = timer->at(2).enable_lo;

			//High and Low reset
			if(timer->at(2).cnt & 0x200) { timer->at(2).counter &= (timer->at(2).full_mode) ? 0x00 : 0xFF; timer->at(2).clock_hi = 0; }
			if(timer->at(2).cnt & 0x2) { timer->at(2).counter &= 0xFF00; timer->at(2).clock_lo = 0; }
			break;

		//Timer 1 Preset Value
		case TIMER1_PRESET_LO:
		case TIMER1_PRESET_HI:
			timer->at(0).reload_value = (memory_map[TIMER1_PRESET_HI] << 8) | memory_map[TIMER1_PRESET_LO];
			break;

		//Timer 2 Preset Value
		case TIMER2_PRESET_LO:
		case TIMER2_PRESET_HI:
			timer->at(1).reload_value = (memory_map[TIMER2_PRESET_HI] << 8) | memory_map[TIMER2_PRESET_LO];
			break;

		//Timer 3 Preset Value
		case TIMER3_PRESET_LO:
		case TIMER3_PRESET_HI:
			timer->at(2).reload_value = (memory_map[TIMER3_PRESET_HI] << 8) | memory_map[TIMER3_PRESET_LO];
			if(timer->at(2).reload_value) { apu_stat->duty_cycle = double(timer->at(2).pivot) / timer->at(2).reload_value; }
			break;

		//Timer 3 Pivot Value
		case TIMER3_PIVOT_LO:
		case TIMER3_PIVOT_HI:
			timer->at(2).pivot = (memory_map[TIMER3_PIVOT_HI] << 8) | memory_map[TIMER3_PIVOT_LO];
			if(timer->at(2).reload_value) { apu_stat->duty_cycle = double(timer->at(2).pivot) / timer->at(2).reload_value; }
			break;

		//256Hz Timer Control
		case TIMER256_CNT:
			timer->at(3).cnt = value & 0x3;
			timer->at(3).enable_lo = (value & 0x1) ? true : false;

			//Reset
			if(value & 0x2)
			{
				timer->at(3).counter = 0;
				timer->at(3).clock_lo = 0;
			}

			break;

		//Peripheral IO Data
		case PM_IO_DATA:
			//Update EEPROM operations
			eeprom.clk = (value & 0x8) ? 1 : 0;
			eeprom.sda = (value & 0x4) ? 1 : 0;
			process_eeprom();

			//Update IR operations
			if(value & 0x2) { memory_map[PM_IO_DATA] |= 0x1; }
			else { process_ir(); }

			//Update Rumble operations
			if((memory_map[PM_IO_DATA] & 0x10) && (memory_map[PM_IO_DIR] & 0x10)) { g_pad->start_rumble(); }
			else if(g_pad->is_rumbling) { g_pad->stop_rumble(); }

			break;

		//Audio Volume
		case PM_AUDIO_VOLUME:
			apu_stat->main_volume = (value & 0x3);
			break;

		//PRC Rate
		case PRC_RATE:
			lcd_stat->prc_rate &= ~0xF;
			lcd_stat->prc_rate |= (value & 0xF);

			//Calculate number of CPU cycles to begin PRC rendering
			switch((lcd_stat->prc_rate >> 1) & 0x7)
			{
				case 0: lcd_stat->prc_rate_div = 3; break;
				case 1: lcd_stat->prc_rate_div = 6; break;
				case 2: lcd_stat->prc_rate_div = 9; break;
				case 3: lcd_stat->prc_rate_div = 12; break;
				case 4: lcd_stat->prc_rate_div = 2; break;
				case 5: lcd_stat->prc_rate_div = 4; break;
				case 6: lcd_stat->prc_rate_div = 6; break;
				case 7: lcd_stat->prc_rate_div = 8; break;
			}

			break;

		//PRC Mode
		case PRC_MODE:
			lcd_stat->prc_mode = value;

			if((lcd_stat->enable_map) && ((value & 0x2) == 0)) { lcd_stat->force_update = true; }

			lcd_stat->invert_map = (value & 0x1) ? true : false;
			lcd_stat->enable_map = (value & 0x2) ? true : false;
			lcd_stat->enable_obj = (value & 0x4) ? true : false;
			lcd_stat->enable_copy = (value & 0x8) ? true : false;
			lcd_stat->map_size = (value >> 4) & 0x3;

			break;

		//PRC Map Address
		case PRC_MAP_LO:
		case PRC_MAP_MID:
		case PRC_MAP_HI:
			lcd_stat->map_addr = (memory_map[PRC_MAP_HI] << 16) | (memory_map[PRC_MAP_MID] << 8) | memory_map[PRC_MAP_LO];
			lcd_stat->map_addr &= 0x1FFFF8;
			break;

		//Scroll Y
		case PRC_SY:
			lcd_stat->scroll_y = value;
			break;

		//Scroll X
		case PRC_SX:
			lcd_stat->scroll_x = value;
			break;

		//PRC Sprite Address
		case PRC_SPR_LO:
		case PRC_SPR_MID:
		case PRC_SPR_HI:
			lcd_stat->obj_addr = (memory_map[PRC_SPR_HI] << 16) | (memory_map[PRC_SPR_MID] << 8) | memory_map[PRC_SPR_LO];
			lcd_stat->obj_addr &= 0x1FFFC0;
			break;

	}
		
}

/****** Write 16-bits to memory ******/
void MIN_MMU::write_u16(u32 address, u16 value)
{
	write_u8(address, (value & 0xFF));
	write_u8((address+1), (value >> 8));
}

/****** Reads ROM file into emulated memory ******/
bool MIN_MMU::read_file(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file_size -= 0x2100;
	file.seekg(0x2100, file.beg);

	u8* ex_mem = &memory_map[0x2100];

	//Read data from the ROM file
	file.read((char*)ex_mem, file_size);

	file.close();

	for(u32 x = (file_size + 0x2100); x < 0x200000; x++) { memory_map[x] = 0xFF; }

	std::string title = "";
	for(u32 x = 0; x < 12; x++) { title += memory_map[MIN_GAME_TITLE + x]; }

	std::string game_code = "";
	for(u32 x = 0; x < 4; x++) { game_code += memory_map[MIN_GAME_CODE + x]; }

	std::cout<<"MMU::Game Title - " << util::make_ascii_printable(title) << "\n";
	std::cout<<"MMU::Game Code - " << util::make_ascii_printable(game_code) << "\n";
	std::cout<<"MMU::ROM Size: " << std::dec << (file_size / 1024) << "KB\n";
	std::cout<<"MMU::ROM CRC32: " << std::hex << util::get_crc32(&memory_map[0x2100], file_size) << "\n";
	std::cout<<"MMU::" << filename << " loaded successfully. \n";

	load_backup(config::save_file);

	return true;
}

/****** Reads BIOS file into emulated memory ******/
bool MIN_MMU::read_bios(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file.seekg(0, file.beg);

	if(file_size > 0x1000)
	{
		std::cout<<"MMU::Warning - BIOS size is too large. Only 4KB will be loaded. \n";
		file_size = 0x1000;
	}

	u8* ex_mem = &memory_map[0];

	//Read data from the ROM file
	file.read((char*)ex_mem, file_size);

	file.close();

	std::cout<<"MMU::BIOS file " << filename << " loaded successfully. \n";

	//Setup IRQ vectors
	for(u32 x = 0; x < 32; x++) { irq_vectors[x] = read_u16(x << 1); }

	return true;
}

/****** Load backup save data ******/
bool MIN_MMU::load_backup(std::string filename)
{
	//Use config save path if applicable
	if(!config::save_path.empty())
	{
		 filename = config::save_path + util::get_filename_from_path(filename);
	}

	std::ifstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " save data could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file.seekg(0, file.beg);
	eeprom.data.resize(file_size, 0x00);

	if(file_size > 0x2000)
	{
		file_size = 0x2000;
		std::cout<<"MMU::Warning - Irregular SRAM backup save size\n";
	}

	//Read data from file
	file.read(reinterpret_cast<char*> (&eeprom.data[0]), file_size);
	file.close();

	std::cout<<"MMU::Loaded save data file " << filename <<  "\n";

	return true;
}

/****** Save backup data ******/
bool MIN_MMU::save_backup(std::string filename)
{
	//Use config save path if applicable
	if(!config::save_path.empty())
	{
		 filename = config::save_path + util::get_filename_from_path(filename);
	}

	std::ofstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " save data could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Read data from file
	file.write(reinterpret_cast<char*> (&eeprom.data[0]), 0x2000);
	file.close();

	std::cout<<"MMU::Wrote save data file " << filename <<  "\n";

	return true;
}

/****** Calculates general purpose timer prescales for oscillator 1 ******/
u32 MIN_MMU::get_prescalar_1(u8 val)
{
	u32 result = 0;

	switch(val & 0x7)
	{
		case 0: result = 2; break;
		case 1: result = 8; break;
		case 2: result = 32; break;
		case 3: result = 64; break;
		case 4: result = 128; break;
		case 5: result = 256; break;
		case 6: result = 1024; break;
		case 7: result = 4096; break;
	}

	return result;
}

/****** Calculates general purpose timer prescales for oscillator 2 ******/
u32 MIN_MMU::get_prescalar_2(u8 val)
{
	u32 result = 0;

	switch(val & 0x7)
	{
		case 0: result = 122; break;
		case 1: result = 244; break;
		case 2: result = 488; break;
		case 3: result = 976; break;
		case 4: result = 1953; break;
		case 5: result = 3906; break;
		case 6: result = 7812; break;
		case 7: result = 15625; break;
	}

	return result;
}

/****** Calculates output frequency of sound via Timer 3 ******/
double MIN_MMU::get_timer3_freq()
{
	//Determine if high or low prescalar is being used
	//8-bit mode = high, 16-bit mode = low
	u8 osc_id = (timer->at(2).full_mode) ? timer->at(2).osc_lo : timer->at(2).osc_hi;
	u8 hi_scale = (memory_map[TIMER3_SCALE] >> 4) & 0x7;
	u8 lo_scale = (memory_map[TIMER3_SCALE] & 0x7);
	
	u8 scale = (timer->at(2).full_mode) ? lo_scale : hi_scale;
	u16 preset = timer->at(2).reload_value + 1;

	double result = 0.0;

	//32768 oscillator
	if(osc_id)
	{
		switch(scale)
		{
			case 0: result = 32768.0 / preset; break;
			case 1: result = 16384.0 / preset; break;
			case 2: result = 8192.0 / preset; break;
			case 3: result = 4096.0 / preset; break;
			case 4: result = 2048.0 / preset; break;
			case 5: result = 1024.0 / preset; break;
			case 6: result = 512.0 / preset; break;
			case 7: result = 256.0 / preset; break;
		}
	}

	//2MHz oscillator 
	else
	{
		switch(scale)
		{
			case 0: result = 2000000.0 / preset; break;
			case 1: result = 500000.0 / preset; break;
			case 2: result = 125000.0 / preset; break;
			case 3: result = 62500.0 / preset; break;
			case 4: result = 31250.0 / preset; break;
			case 5: result = 15625.0 / preset; break;
			case 6: result = 3906.25 / preset; break;
			case 7: result = 976.5625 / preset; break;
		}
	}

	return result;
}

/****** Updates prescalars ******/
void MIN_MMU::update_prescalar(u8 index)
{
	index &= 0x3;
	u32 scale_addr = 0;

	switch(index)
	{
		case 0: scale_addr = TIMER1_SCALE; break;
		case 1: scale_addr = TIMER2_SCALE; break;
		case 2: scale_addr = TIMER3_SCALE; break;
		default: return;
	}

	u8 val = memory_map[scale_addr];

	//Set Oscillator 1 or 2 for low prescalar
	if(timer->at(index).osc_lo) { timer->at(index).prescalar_lo = get_prescalar_2(val & 0x7); }
	else { timer->at(index).prescalar_lo = get_prescalar_1(val & 0x7); }

	//Set Oscillator 1 or 2 for high prescalar
	if(timer->at(index).osc_hi) { timer->at(index).prescalar_hi = get_prescalar_2((val >> 4) & 0x7); }
	else { timer->at(index).prescalar_hi = get_prescalar_1((val >> 4) & 0x7); }
}

/****** Updates the memory-mapped interrupt flags when updating GBE+ internal interrupt flags ******/
void MIN_MMU::update_irq_flags(u32 irq)
{
	switch(irq)
	{
		case PRC_COPY_IRQ:
			master_irq_flags |= PRC_COPY_IRQ;
			memory_map[IRQ_ACT_1] |= 0x80;
			break;

		case PRC_OVERFLOW_IRQ:
			master_irq_flags |= PRC_OVERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x40;
			break;

		case TIMER2_UPPER_UNDERFLOW_IRQ:
			master_irq_flags |= TIMER2_UPPER_UNDERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x20;
			break;

		case TIMER2_LOWER_UNDERFLOW_IRQ:
			master_irq_flags |= TIMER2_LOWER_UNDERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x10;
			break;

		case TIMER1_UPPER_UNDERFLOW_IRQ:
			master_irq_flags |= TIMER1_UPPER_UNDERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x08;
			break;

		case TIMER1_LOWER_UNDERFLOW_IRQ:
			master_irq_flags |= TIMER1_LOWER_UNDERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x04;
			break;

		case TIMER3_UPPER_UNDERFLOW_IRQ:
			master_irq_flags |= TIMER3_UPPER_UNDERFLOW_IRQ;
			memory_map[IRQ_ACT_1] |= 0x02;
			break;

		case TIMER3_PIVOT_IRQ:
			master_irq_flags |= TIMER3_PIVOT_IRQ;
			memory_map[IRQ_ACT_1] |= 0x01;
			break;

		case TIMER_32HZ_IRQ:
			master_irq_flags |= TIMER_32HZ_IRQ;
			memory_map[IRQ_ACT_2] |= 0x20;
			break;

		case TIMER_8HZ_IRQ:
			master_irq_flags |= TIMER_8HZ_IRQ;
			memory_map[IRQ_ACT_2] |= 0x10;
			break;

		case TIMER_2HZ_IRQ:
			master_irq_flags |= TIMER_2HZ_IRQ;
			memory_map[IRQ_ACT_2] |= 0x08;
			break;

		case TIMER_1HZ_IRQ:
			master_irq_flags |= TIMER_1HZ_IRQ;
			memory_map[IRQ_ACT_2] |= 0x04;
			break;

		case CART_EJECT_IRQ:
			master_irq_flags |= CART_EJECT_IRQ;
			memory_map[IRQ_ACT_2] |= 0x02;
			break;

		case CART_IRQ:
			master_irq_flags |= CART_IRQ;
			memory_map[IRQ_ACT_2] |= 0x01;
			break;

		case POWER_KEY_IRQ:
			master_irq_flags |= POWER_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x80;
			break;

		case RIGHT_KEY_IRQ:
			master_irq_flags |= RIGHT_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x40;
			break;

		case LEFT_KEY_IRQ:
			master_irq_flags |= LEFT_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x20;
			break;

		case DOWN_KEY_IRQ:
			master_irq_flags |= DOWN_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x10;
			break;

		case UP_KEY_IRQ:
			master_irq_flags |= UP_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x08;
			break;

		case C_KEY_IRQ:
			master_irq_flags |= C_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x04;
			break;

		case B_KEY_IRQ:
			master_irq_flags |= B_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x02;
			break;

		case A_KEY_IRQ:
			master_irq_flags |= A_KEY_IRQ;
			memory_map[IRQ_ACT_3] |= 0x01;
			break;

		case IR_RECEIVER_IRQ:
			master_irq_flags |= IR_RECEIVER_IRQ;
			memory_map[IRQ_ACT_4] |= 0x80;
			break;

		case SHOCK_SENSOR_IRQ:
			master_irq_flags |= SHOCK_SENSOR_IRQ;
			memory_map[IRQ_ACT_4] |= 0x40;
			break;
	}
}
	
/****** Updates EEPROM ******/
void MIN_MMU::process_eeprom()
{
	//When clock goes from LOW to HIGH, clock data bit
	if((eeprom.last_clk == 0) && (eeprom.clk == 1))
	{
		eeprom.clock_data = true;
	}

	//When clock is HIGH and data goes from LOW to HIGH, signal stop condition
	if((eeprom.last_clk == 1) && (eeprom.clk == 1) && (eeprom.last_sda == 0) && (eeprom.sda == 1))
	{
		eeprom.stop_cond = true;
		eeprom.start_cond = false;
		eeprom.state = 0;
	}

	//When clock is HIGH and data goes from HIGH to LOW, signal start condition
	if((eeprom.last_clk == 1) && (eeprom.clk == 1) && (eeprom.last_sda == 1) && (eeprom.sda == 0))
	{
		eeprom.start_cond = true;
		eeprom.stop_cond = false;
		eeprom.state = 0;
		eeprom.control = 0;
	}

	//EEPROM operations
	switch(eeprom.state)
	{
		//Wait for start condition
		case 0:
			if(eeprom.start_cond)
			{
				eeprom.state = 1;
				eeprom.start_cond = false;
				eeprom.clock_data = false;
				eeprom.addr_bit = 7;
			}

			break;

		//Wait for control byte (8-bits)
		case 1:
			if(eeprom.clock_data)
			{
				eeprom.clock_data = false;

				if(eeprom.sda) { eeprom.control |= (1 << eeprom.addr_bit); }

				if(eeprom.addr_bit == 0)
				{
					eeprom.read_mode = (eeprom.control & 0x1) ? true : false;
					eeprom.state = 2;
				}

				else { eeprom.addr_bit--; }
			}

			break;

		//Wait for ACK
		case 2:
			if(eeprom.clock_data)
			{
 				memory_map[PM_IO_DATA] &= ~0x04;
				eeprom.clock_data = false;

				//Handle read operations
				if(eeprom.read_mode)
				{
					eeprom.data_byte = eeprom.data[eeprom.addr];
					eeprom.addr_bit = 7;
					eeprom.state = 9;
				}

				//Handle write operations
				else
				{
					eeprom.addr_bit = 15;
					eeprom.addr = 0;
					eeprom.state = 3;
				}
			}

			break;

		//Grab HI byte of EEPROM address
		case 3:
			if(eeprom.clock_data)
			{
				eeprom.clock_data = false;

				if(eeprom.sda) { eeprom.addr |= (1 << eeprom.addr_bit); }

				if(eeprom.addr_bit == 8)
				{
					eeprom.state = 4;
					eeprom.addr_bit--;
				}

				else { eeprom.addr_bit--; }
			}

			break;

		//Wait for ACK between EEPROM address
		case 4:
			if(eeprom.clock_data)
			{
				memory_map[PM_IO_DATA] &= ~0x04;
				eeprom.clock_data = false;
				eeprom.state = 5;
			}

			break;

		//Grab LO byte of EEPROM address
		case 5:
			if(eeprom.clock_data)
			{
				eeprom.clock_data = false;

				if(eeprom.sda) { eeprom.addr |= (1 << eeprom.addr_bit); }

				if(eeprom.addr_bit == 0)
				{
					eeprom.state = 6;
					eeprom.addr &= 0x1FFF;
				}
				
				else { eeprom.addr_bit--; }
			}

			break;

		//Wait for ACK after EEPROM address
		case 6:
			if(eeprom.clock_data)
			{
				memory_map[PM_IO_DATA] &= ~0x04;
				eeprom.clock_data = false;
				eeprom.state = 7;
				eeprom.addr_bit = 7;
				eeprom.data_byte = 0;
			}

			break;

		//Grab byte to write
		case 7:
			if(eeprom.clock_data)
			{
				eeprom.clock_data = false;

				if(eeprom.sda) { eeprom.data_byte |= (1 << eeprom.addr_bit); }

				//Write byte
				if(eeprom.addr_bit == 0)
				{
					save_eeprom = true;
					eeprom.data[eeprom.addr++] = eeprom.data_byte;
					eeprom.state = 8;
				}
				
				else { eeprom.addr_bit--; }
			}

			break;

		//Wait ACK before writing next byte
		case 8:
			if(eeprom.clock_data)
			{
				memory_map[PM_IO_DATA] &= ~0x04;
				eeprom.clock_data = false;

				eeprom.addr_bit = 7;
				eeprom.data_byte = 0;
				eeprom.state = 7;
			}

			break;

		//Send byte to PM
		case 9:
			if(eeprom.clock_data)
			{
				eeprom.clock_data = false;

				if(eeprom.data_byte & (1 << eeprom.addr_bit)) { memory_map[PM_IO_DATA] |= 0x04; }
				else { memory_map[PM_IO_DATA] &= ~0x04; }

				//Grab next byte for reading
				if(eeprom.addr_bit == 0)
				{
					eeprom.addr_bit = 7;
					eeprom.data_byte = eeprom.data[++eeprom.addr];
					eeprom.state = 10;
				}

				else { eeprom.addr_bit--; }
			}

			break;

		//Wait for ACK or STOP signal
		case 10:
			if(eeprom.clock_data)
			{
				memory_map[PM_IO_DATA] &= ~0x04;
				eeprom.clock_data = false;
				eeprom.state = 9;
			}

			break;
	}	

	eeprom.last_clk = eeprom.clk;
	eeprom.last_sda = eeprom.sda;
}

/****** Points the MMU to an lcd_data structure (FROM THE LCD ITSELF) ******/
void MIN_MMU::set_lcd_data(min_lcd_data* ex_lcd_stat) { lcd_stat = ex_lcd_stat; }

/****** Points the MMU to an apu_data structure (FROM THE APU ITSELF) ******/
void MIN_MMU::set_apu_data(min_apu_data* ex_apu_stat) { apu_stat = ex_apu_stat; }
