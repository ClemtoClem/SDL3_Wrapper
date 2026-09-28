
#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include "../defines.hpp"
#include "../state_archive.hpp"
#include "sdl3/iostream.hpp"

namespace emulator_demo {

class Core;
enum NdsCmdMode {
	CMD_NONE = 0,
	CMD_HEADER,
	CMD_CHIP,
	CMD_SECURE,
	CMD_DATA
};

class Cartridge {
public:
	Cartridge(Core *core) : core(core) {}
	
	~Cartridge();

	virtual bool loadRom(const String& path);

	void writeSave();

	void trimRom();

	void resizeSave(int newSize, bool dirty = true);

	/**
	 * @brief Sérialise/désérialise l'état commun à toutes les cartouches.
	 *
	 * Le contenu de la ROM elle-même n'est pas sauvegardé : seul le chemin
	 * (romName) est conservé, et la ROM est rechargée depuis le disque au
	 * chargement (via loadRom(), qui appelle la version virtuelle dérivée).
	 * Le contenu de la sauvegarde (save) est en revanche sérialisé.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void ioState(StateArchive &archive);

	int getRomSize() const {
		return romSize;
	}

	int getSaveSize() const {
		return saveSize;
	}

protected:
	Core *core;
	sdl3::IOStream romFile;
	uint8_t *rom = nullptr, *save = nullptr;
	int romSize = 0, saveSize = 0;
	bool saveDirty = false;
	std::mutex mutex;
	uint32_t romMask = 0;
	/// false si la section n'a pas pu être allouée (ROM laissée inchangée).
	bool loadRomSection(size_t offset, size_t size);

private:
	String romName, saveName;
};


class CartridgeNds : public Cartridge {
public:
	CartridgeNds(Core *core);

	bool loadRom(const String& path);

	void directBoot();

	uint16_t readAuxSpiCnt(bool cpu) const {
		return auxSpiCnt[cpu];
	}

	uint8_t readAuxSpiData(bool cpu) const {
		return auxSpiData[cpu];
	}

	uint32_t readRomCtrl(bool cpu) const {
		return romCtrl[cpu];
	}

	uint32_t readRomDataIn(bool cpu);
	void writeAuxSpiCnt(bool cpu, uint16_t mask, uint16_t value);
	void writeAuxSpiData(bool cpu, uint8_t value);
	void writeRomCtrl(bool cpu, uint32_t mask, uint32_t value);
	void writeRomCmdOutL(bool cpu, uint32_t mask, uint32_t value);
	void writeRomCmdOutH(bool cpu, uint32_t mask, uint32_t value);

	/**
	 * @brief Sérialise/désérialise l'état spécifique aux cartouches NDS.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void ioState(StateArchive &archive);

private:
	uint32_t romCode = 0;
	bool romEncrypted = false;
	NdsCmdMode cmdMode = CMD_NONE;
	uint32_t encTable[0x412] = {};
	uint32_t encCode[3] = {};
	uint32_t romAddrReal[2] = {}, romAddrVirt[2] = {};
	uint16_t blockSize[2] = {}, readCount[2] = {};
	uint32_t wordCycles[2] = {};
	bool encrypted[2] = {};
	uint8_t auxCommand[2] = {};
	uint32_t auxAddress[2] = {};
	int auxWriteCount[2] = {};
	uint16_t auxSpiCnt[2] = {};
	uint8_t auxSpiData[2] = {};
	uint32_t romCtrl[2] = {};
	uint64_t romCmdOut[2] = {};
	std::function<void()> wordReadyTasks[2];
	uint64_t encrypt64(uint64_t value);
	uint64_t decrypt64(uint64_t value);
	void initKeycode(int level);
	void applyKeycode();
	void wordReady(bool cpu);
};

class CartridgeGba : public Cartridge {
public:
	CartridgeGba(Core *core) : Cartridge(core) {}
	bool loadRom(const String& path);
	uint8_t *getRom(uint32_t address);
	bool isEeprom(uint32_t address);
	uint8_t eepromRead();
	void eepromWrite(uint8_t value);
	uint8_t sramRead(uint32_t address);
	void sramWrite(uint32_t address, uint8_t value);

	/**
	 * @brief Sérialise/désérialise l'état spécifique aux cartouches GBA.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void ioState(StateArchive &archive);

private:
	int eepromCount = 0;
	uint16_t eepromCmd = 0;
	uint64_t eepromData = 0;
	bool eepromDone = false;
	uint8_t flashCmd = 0;
	bool bankSwap = false;
	bool flashErase = false;
	bool findString(String string);
};

FORCE_INLINE uint8_t *CartridgeGba::getRom(uint32_t address) {
	return ((address &= romMask) < uint32_t(romSize)) ? &rom[address] : nullptr;
}

FORCE_INLINE bool CartridgeGba::isEeprom(uint32_t address) {
	return (saveSize == -1 || saveSize == 0x200 || saveSize == 0x2000) &&
				 (romSize <= 0x1000000 || address >= (uint32_t)0x0DFFFF00);
}

} // namespace emulator_demo

#endif
