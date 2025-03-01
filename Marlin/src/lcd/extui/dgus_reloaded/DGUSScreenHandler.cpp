/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2021 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_RELOADED)

#include "DGUSScreenHandler.h"

#include "DGUSDisplay.h"
#include "definition/DGUS_ScreenAddrList.h"
#include "definition/DGUS_ScreenSetup.h"

#include "../../../gcode/queue.h"

uint8_t DGUSScreenHandler::debug_count = 0;

#if ENABLED(SDSUPPORT)
  ExtUI::FileList DGUSScreenHandler::filelist;
  uint16_t DGUSScreenHandler::filelist_offset = 0;
  int16_t DGUSScreenHandler::filelist_selected = -1;
#endif

DGUS_Data::StepSize DGUSScreenHandler::offset_steps = DGUS_Data::StepSize::MMP1;
DGUS_Data::StepSize DGUSScreenHandler::move_steps = DGUS_Data::StepSize::MM10;

uint16_t DGUSScreenHandler::probing_icons[] = { 0, 0 };
uint8_t DGUSScreenHandler::levelingPoint = 0;

DGUS_Data::Extruder DGUSScreenHandler::filament_extruder = DGUS_Data::Extruder::CURRENT;
uint16_t DGUSScreenHandler::filament_length = DGUS_DEFAULT_FILAMENT_LEN;

char DGUSScreenHandler::gcode[] = "";

DGUS_Data::Heater DGUSScreenHandler::pid_heater = DGUS_Data::Heater::H0;
uint16_t DGUSScreenHandler::pid_temp = DGUS_PLA_TEMP_HOTEND;
uint8_t DGUSScreenHandler::pid_cycles = 5;

bool DGUSScreenHandler::settings_ready = false;
bool DGUSScreenHandler::booted = false;

DGUS_Screen DGUSScreenHandler::current_screen = DGUS_Screen::BOOT;
DGUS_Screen DGUSScreenHandler::new_screen = DGUS_Screen::BOOT;
bool DGUSScreenHandler::full_update = false;

DGUS_Screen DGUSScreenHandler::wait_return_screen = DGUS_Screen::HOME;
bool DGUSScreenHandler::wait_continue = false;

bool DGUSScreenHandler::leveling_active = false;

millis_t DGUSScreenHandler::status_expire = 0;
millis_t DGUSScreenHandler::eeprom_save = 0;

#define en 1
#define fr 2
#define fr_na 2
#if LCD_LANGUAGE == fr //  French version for status messages

const char DGUS_MSG_HOMING_REQUIRED[] PROGMEM = "Retour a l'origine necessaire...",
                                      DGUS_MSG_BUSY[] PROGMEM = "Occupe...",
                                      DGUS_MSG_UNDEF[] PROGMEM = "-",
                                      DGUS_MSG_HOMING[] PROGMEM = "Retour a l'origine...",
                                      DGUS_MSG_FW_OUTDATED[] PROGMEM = "Mise a jour DWIN GUI/OS necessaire",
                                      DGUS_MSG_LEVELING_SUCCESS[] PROGMEM = "Nivellement realise avec succes",
                                      DGUS_MSG_LEVELING_FAILED[] PROGMEM = "Echec du nivellement...",
                                      DGUS_MSG_EEPROM_REINIT[] PROGMEM = "Reinitialisation de l'EEPROM",
                                      DGUS_MSG_EEPROM_FAILED[] PROGMEM = "Echec ecriture de l'EEPROM",
                                      DGUS_MSG_EEPROM_READ_FAILED[] PROGMEM = "Echec lecture de l'EEPROM",
                                      DGUS_MSG_FILAMENT_RUNOUT[] PROGMEM = "Filament sur buse E%d";

#else
const char DGUS_MSG_HOMING_REQUIRED[] PROGMEM = "Homing necessary...",
                                      DGUS_MSG_BUSY[] PROGMEM = "Busy...",
                                      DGUS_MSG_UNDEF[] PROGMEM = "-",
                                      DGUS_MSG_HOMING[] PROGMEM = "Homing...",
                                      DGUS_MSG_FW_OUTDATED[] PROGMEM = "Update to DWIN GUI/OS required",
                                      DGUS_MSG_LEVELING_SUCCESS[] PROGMEM = "Leveling success",
                                      DGUS_MSG_LEVELING_FAILED[] PROGMEM = "Leveling failed...",
                                      DGUS_MSG_EEPROM_REINIT[] PROGMEM = "Reinitializing EEPROM",
                                      DGUS_MSG_EEPROM_FAILED[] PROGMEM = "Failed to write EEPROM",
                                      DGUS_MSG_EEPROM_READ_FAILED[] PROGMEM = "Failed to read EEPROM",
                                      DGUS_MSG_FILAMENT_RUNOUT[] PROGMEM = "Filament runout E%d",
                                      DGUS_MSG_ABL_REQUIRED[] PROGMEM = "Auto bed leveling required";

#endif
#undef en
#undef fr_na
#undef fr

const char DGUS_CMD_HOME[] PROGMEM = "G28",
           DGUS_CMD_EEPROM_SAVE[] PROGMEM = "M500";

//PauseMode pause_mode = PAUSE_MODE_PAUSE_PRINT;

void DGUSScreenHandler::Init() {
  dgus_display.Init();

  MoveToScreen(DGUS_Screen::BOOT, true);
}

void DGUSScreenHandler::Ready() {
  dgus_display.PlaySound(1);
}

void DGUSScreenHandler::Loop() {
  if (!settings_ready || current_screen == DGUS_Screen::KILL)     {
    return;
  }

  const millis_t ms = ExtUI::safe_millis();
  static millis_t next_event_ms = 0;

  if (new_screen != DGUS_Screen::BOOT)     {
    const DGUS_Screen screen = new_screen;
    new_screen = DGUS_Screen::BOOT;

    if (current_screen == screen)         {
      TriggerFullUpdate();
    }
    else         {
      MoveToScreen(screen);
    }
    return;
  }

  if (!booted && ELAPSED(ms, 3000))     {
    booted = true;

    if (current_screen == DGUS_Screen::BOOT)         {
      MoveToScreen(DGUS_Screen::HOME);
    }
    return;
  }

  if (ELAPSED(ms, next_event_ms) || full_update)     {
    next_event_ms = ms + DGUS_UPDATE_INTERVAL_MS;

    if (!SendScreenVPData(current_screen, full_update))         {
      DEBUG_ECHOLNPGM("SendScreenVPData failed");
    }
    return;
  }

  if (current_screen == DGUS_Screen::WAIT && ((wait_continue && !wait_for_user) || (!wait_continue && IsPrinterIdle())))     {
    MoveToScreen(wait_return_screen, true);
    return;
  }

#if HAS_LEVELING
  if (current_screen == DGUS_Screen::LEVELING_PROBING && IsPrinterIdle())     {
    dgus_display.PlaySound(3);

    SetStatusMessage(ExtUI::getMeshValid() ? 
                       FPSTR(DGUS_MSG_LEVELING_SUCCESS)
                     : FPSTR(DGUS_MSG_LEVELING_FAILED));

    MoveToScreen(DGUS_Screen::LEVELING_AUTOMATIC);
    return;
  }
#endif

  if (status_expire > 0 && ELAPSED(ms, status_expire))     {
    SetStatusMessage(FPSTR(NUL_STR), 0);
    return;
  }

  if (eeprom_save > 0 && ELAPSED(ms, eeprom_save) && IsPrinterIdle())     {
    eeprom_save = 0;

    queue.enqueue_now_P(DGUS_CMD_EEPROM_SAVE);
    return;
  }

  dgus_display.Loop();
}

void DGUSScreenHandler::PrinterKilled(FSTR_P const error, FSTR_P const component) {
  SetMessageLinePGM(FTOP(error), 1);
  SetMessageLinePGM(FTOP(component), 2);
  SetMessageLinePGM(NUL_STR, 3);
  SetMessageLinePGM(GET_TEXT(MSG_PLEASE_RESET), 4);

  dgus_display.PlaySound(3, 1, 200);

  MoveToScreen(DGUS_Screen::KILL, true);
}

void DGUSScreenHandler::UserConfirmRequired(const char* const msg) {
  dgus_screen_handler.SetMessageLinePGM(NUL_STR, 1);
  dgus_screen_handler.SetMessageLine(msg, 2);
  dgus_screen_handler.SetMessageLinePGM(NUL_STR, 3);
  dgus_screen_handler.SetMessageLinePGM(NUL_STR, 4);

  dgus_display.PlaySound(3);

  dgus_screen_handler.ShowWaitScreen(current_screen, true);
}

void DGUSScreenHandler::SettingsReset() {
  dgus_display.SetVolume(DGUS_DEFAULT_VOLUME);
  dgus_display.SetBrightness(DGUS_DEFAULT_BRIGHTNESS);

  if (!settings_ready)     {
    settings_ready = true;

    Ready();
  }

  SetStatusMessage(FPSTR(DGUS_MSG_EEPROM_REINIT));
}

void DGUSScreenHandler::StoreSettings(char* buff) {
  eeprom_data_t data;

  static_assert(sizeof(data) <= ExtUI::eeprom_data_size, "sizeof(eeprom_data_t) > eeprom_data_size.");

  data.initialized = true;
  data.volume = dgus_display.GetVolume();
  data.brightness = dgus_display.GetBrightness();
#if HAS_LEVELING
  data.abl = (ExtUI::getLevelingActive() && ExtUI::getMeshValid());
#endif
  memcpy(buff, &data, sizeof(data));
}

void DGUSScreenHandler::LoadSettings(const char* buff) {
  eeprom_data_t data;

  static_assert(sizeof(data) <= ExtUI::eeprom_data_size, "sizeof(eeprom_data_t) > eeprom_data_size.");

  memcpy(&data, buff, sizeof(data));

  dgus_display.SetVolume(data.initialized ? data.volume : DGUS_DEFAULT_VOLUME);
  dgus_display.SetBrightness(data.initialized ? data.brightness : DGUS_DEFAULT_BRIGHTNESS);

#if HAS_LEVELING
  if (data.initialized) {
    leveling_active = (data.abl && ExtUI::getMeshValid());
    ExtUI::setLevelingActive(leveling_active);
  }
#endif
}

void DGUSScreenHandler::ConfigurationStoreWritten(bool success) {
  if (!success)     {
    SetStatusMessage(FPSTR(DGUS_MSG_EEPROM_FAILED));
  }
}

void DGUSScreenHandler::ConfigurationStoreRead(bool success) {
  if (!success)     {
    SetStatusMessage(FPSTR(DGUS_MSG_EEPROM_READ_FAILED));
  }
  else if (!settings_ready)     {
    settings_ready = true;

    Ready();
  }
}

void DGUSScreenHandler::PlayTone(const uint16_t frequency, const uint16_t duration) {
  UNUSED(duration);

  if (frequency >= 1 && frequency <= 255)     {
    if (duration >= 1 && duration <= 255)         {
      dgus_display.PlaySound((uint8_t)frequency, (uint8_t)duration);
    }
    else         {
      dgus_display.PlaySound((uint8_t)frequency);
    }
  }
}

void DGUSScreenHandler::MoveToLevelPoint() {
  constexpr float lfrb[4] = LEVEL_CORNERS_INSET_LFRB;
  float x, y;

  switch (levelingPoint)     {
  default:
    return;
  case 1:
    x = DGUS_LEVEL_CENTER_X;
    y = DGUS_LEVEL_CENTER_Y;
    break;
  case 2:
    x = X_MIN_POS + lfrb[0];
    y = Y_MIN_POS + lfrb[1];
    break;
  case 3:
    x = X_MAX_POS - lfrb[2];
    y = Y_MIN_POS + lfrb[1];
    break;
  case 4:
    x = X_MAX_POS - lfrb[2];
    y = Y_MAX_POS - lfrb[3];
    break;
  case 5:
    x = X_MIN_POS + lfrb[0];
    y = Y_MAX_POS - lfrb[3];
    break;
  };

  if (ExtUI::getAxisPosition_mm(ExtUI::Z) < Z_MIN_POS + LEVEL_CORNERS_Z_HOP)     {
    ExtUI::setAxisPosition_mm(Z_MIN_POS + LEVEL_CORNERS_Z_HOP, ExtUI::Z);
  }
  ExtUI::setAxisPosition_mm(x, ExtUI::X);
  ExtUI::setAxisPosition_mm(y, ExtUI::Y);
  ExtUI::setAxisPosition_mm(Z_MIN_POS + LEVEL_CORNERS_HEIGHT, ExtUI::Z);
  levelingPoint = 0;
}

#if HAS_LEVELING
void DGUSScreenHandler::MeshUpdate(const int8_t xpos, const int8_t ypos) {
  // if (current_screen == DGUS_Screen::LEVELING_PROBING || current_screen == DGUS_Screen::LEVELING_AUTOMATIC) {
  //   TriggerFullUpdate();
  // }
  uint8_t point = ypos * GRID_MAX_POINTS_X + xpos;
  probing_icons[point < 16 ? 0 : 1] |= (1U << (point % 16));

  if (xpos >= GRID_MAX_POINTS_X - 1 && ypos >= GRID_MAX_POINTS_Y - 1 && !ExtUI::getMeshValid())     {
    probing_icons[0] = 0;
    probing_icons[1] = 0;
  }

  TriggerFullUpdate();
}
#endif

void DGUSScreenHandler::PrintTimerStarted() {
  TriggerScreenChange(DGUS_Screen::PRINT_STATUS);
}

void DGUSScreenHandler::PrintTimerPaused() {
  dgus_display.PlaySound(3);

  TriggerFullUpdate();
}

void DGUSScreenHandler::PrintTimerStopped() {
  if (current_screen != DGUS_Screen::PRINT_STATUS && current_screen != DGUS_Screen::PRINT_ADJUST)     {
    return;
  }
  dgus_display.PlaySound(3);
  TriggerScreenChange(DGUS_Screen::PRINT_FINISHED);
}

void DGUSScreenHandler::PrintFinished() {
}

void DGUSScreenHandler::FilamentRunout(const ExtUI::extruder_t extruder) {
  char buffer[21];
  snprintf_P(buffer, sizeof(buffer), DGUS_MSG_FILAMENT_RUNOUT, extruder);

  SetStatusMessage(buffer);

  dgus_display.PlaySound(3);
}

#if ENABLED(SDSUPPORT)

void DGUSScreenHandler::SDCardInserted() {
  if (current_screen == DGUS_Screen::HOME)     {
    TriggerScreenChange(DGUS_Screen::PRINT);
  }
}

void DGUSScreenHandler::SDCardRemoved() {
  if (current_screen == DGUS_Screen::PRINT)     {
    TriggerScreenChange(DGUS_Screen::HOME);
  }
}

  void DGUSScreenHandler::SDCardError() {
    SetStatusMessage(GET_TEXT_F(MSG_MEDIA_READ_ERROR));

  if (current_screen == DGUS_Screen::PRINT)     {
    TriggerScreenChange(DGUS_Screen::HOME);
  }
}

#endif // SDSUPPORT

#if ENABLED(POWER_LOSS_RECOVERY)

void DGUSScreenHandler::PowerLossResume() {
  MoveToScreen(DGUS_Screen::POWERLOSS, true);
}

#endif // POWER_LOSS_RECOVERY

#if HAS_PID_HEATING

  void DGUSScreenHandler::PidTuning(const ExtUI::result_t rst) {
    switch (rst) {
      case ExtUI::PID_STARTED:
        SetStatusMessage(GET_TEXT_F(MSG_PID_AUTOTUNE));
        break;
      case ExtUI::PID_BAD_EXTRUDER_NUM:
        SetStatusMessage(GET_TEXT_F(MSG_PID_BAD_EXTRUDER_NUM));
        break;
      case ExtUI::PID_TEMP_TOO_HIGH:
        SetStatusMessage(GET_TEXT_F(MSG_PID_TEMP_TOO_HIGH));
        break;
      case ExtUI::PID_TUNING_TIMEOUT:
        SetStatusMessage(GET_TEXT_F(MSG_PID_TIMEOUT));
        break;
      case ExtUI::PID_DONE:
        SetStatusMessage(GET_TEXT_F(MSG_PID_AUTOTUNE_DONE));
        break;
      default:
        return;
    }

    dgus_display.PlaySound(3);
  }


#endif // HAS_PID_HEATING

// Pause message - equivalent of ui.pause_show_message()
void DGUSScreenHandler::ShowPauseMessage(PauseMessage message, PauseMode mode) {
  PGM_P msg = NUL_STR;
  PGM_P header = NUL_STR;
  bool waitForInput = false;

  if (mode != PAUSE_MODE_SAME)     {
    pause_mode = mode;
  }

  switch (pause_mode)     {
  case PAUSE_MODE_CHANGE_FILAMENT:
    header = GET_TEXT(MSG_FILAMENT_CHANGE_HEADER);
    break;
  case PAUSE_MODE_LOAD_FILAMENT:
    header = GET_TEXT(MSG_FILAMENT_CHANGE_HEADER_LOAD);
    break;
  case PAUSE_MODE_UNLOAD_FILAMENT:
    header = GET_TEXT(MSG_FILAMENT_CHANGE_HEADER_UNLOAD);
    break;
  default:
    header = GET_TEXT(MSG_FILAMENT_CHANGE_HEADER_PAUSE);
    break;
  }

  switch (message)     {
  case PAUSE_MESSAGE_PARKING:
    msg = GET_TEXT(MSG_PAUSE_PRINT_PARKING);
    break;
  case PAUSE_MESSAGE_CHANGING:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_INIT);
    break;
  case PAUSE_MESSAGE_UNLOAD:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_UNLOAD);
    break;
  case PAUSE_MESSAGE_WAITING:
    msg = GET_TEXT(MSG_ADVANCED_PAUSE_WAITING);
    waitForInput = true;
    break;
  case PAUSE_MESSAGE_INSERT:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_INSERT);
    waitForInput = true;
    break;
  case PAUSE_MESSAGE_LOAD:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_LOAD);
    break;
  case PAUSE_MESSAGE_PURGE:
  #if ENABLED(ADVANCED_PAUSE_CONTINUOUS_PURGE)
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_CONT_PURGE);
  #else
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_PURGE);
  #endif
    break;
  case PAUSE_MESSAGE_RESUME:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_RESUME);
    break;
  case PAUSE_MESSAGE_HEAT:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_HEAT);
    waitForInput = true;
    break;
  case PAUSE_MESSAGE_HEATING:
    msg = GET_TEXT(MSG_FILAMENT_CHANGE_HEATING);
    break;
  case PAUSE_MESSAGE_OPTION:
    pause_menu_response = PAUSE_RESPONSE_RESUME_PRINT; // continue without asking user
    break;
  case PAUSE_MESSAGE_STATUS:
    break;
  default:
    break;
  }

  if (msg == NUL_STR)     {
    MoveToScreen(DGUS_Screen::PRINT_STATUS, true); // ??
    return;
  }

  PGM_P const msg2 = msg;
  PGM_P const msg3 = msg2 + strlen_P(msg2) + 1;
  PGM_P const msg4 = msg3 + strlen_P(msg3) + 1;

  SetMessageLinePGM(header, 1);
  SetMessageLinePGM(msg2, 2);
  SetMessageLinePGM(msg3, 3);
  SetMessageLinePGM(msg4, 4);
  ShowWaitScreen(DGUS_Screen::PRINT_STATUS, waitForInput);
}

void DGUSScreenHandler::SetMessageLine(const char* msg, uint8_t line) {
  DGUS_Addr vp;
  DGUS_Addr sp;
  const int16_t* box;
  switch (line)     {
  default:
    return;
  case 1:
    vp = DGUS_Addr::MESSAGE_Line1;
    sp = DGUS_Addr::SP_MSG_LINE1;
    box = WAIT_Message_Line1_Box;
    break;
  case 2:
    vp = DGUS_Addr::MESSAGE_Line2;
    sp = DGUS_Addr::SP_MSG_LINE2;
    box = WAIT_Message_Line2_Box;
    break;
  case 3:
    vp = DGUS_Addr::MESSAGE_Line3;
    sp = DGUS_Addr::SP_MSG_LINE3;
    box = WAIT_Message_Line3_Box;
    break;
  case 4:
    vp = DGUS_Addr::MESSAGE_Line4;
    sp = DGUS_Addr::SP_MSG_LINE4;
    box = WAIT_Message_Line4_Box;
    break;
  }
  dgus_display.WriteString((uint16_t)vp, msg, DGUS_LINE_LEN, true, false, false);
  SetTextSize(sp, _MIN(strlen(msg), DGUS_LINE_LEN), box, true);
}

void DGUSScreenHandler::SetMessageLinePGM(PGM_P msg, uint8_t line) {
  DGUS_Addr vp;
  DGUS_Addr sp;
  const int16_t* box;
  switch (line)     {
  default:
    return;
  case 1:
    vp = DGUS_Addr::MESSAGE_Line1;
    sp = DGUS_Addr::SP_MSG_LINE1;
    box = WAIT_Message_Line1_Box;
    break;
  case 2:
    vp = DGUS_Addr::MESSAGE_Line2;
    sp = DGUS_Addr::SP_MSG_LINE2;
    box = WAIT_Message_Line2_Box;
    break;
  case 3:
    vp = DGUS_Addr::MESSAGE_Line3;
    sp = DGUS_Addr::SP_MSG_LINE3;
    box = WAIT_Message_Line3_Box;
    break;
  case 4:
    vp = DGUS_Addr::MESSAGE_Line4;
    sp = DGUS_Addr::SP_MSG_LINE4;
    box = WAIT_Message_Line4_Box;
    break;
  }
  dgus_display.WriteStringPGM((uint16_t)vp, msg, DGUS_LINE_LEN, true, false, false);
  SetTextSize(sp, _MIN(strlen_P(msg), DGUS_LINE_LEN), box, true);
}

void DGUSScreenHandler::SetStatusMessage(const char* msg, const millis_t duration) {
  dgus_display.WriteString((uint16_t)DGUS_Addr::MESSAGE_Status, msg, DGUS_STATUS_LEN, false, true);

  status_expire = (duration > 0 ? ExtUI::safe_millis() + duration : 0);
}

void DGUSScreenHandler::SetStatusMessage(FSTR_P const fmsg, const millis_t duration) {
  dgus_display.WriteStringPGM((uint16_t)DGUS_Addr::MESSAGE_Status, FTOP(fmsg), DGUS_STATUS_LEN, false, true);

  status_expire = (duration > 0 ? ExtUI::safe_millis() + duration : 0);
}

void DGUSScreenHandler::SetTextSize(DGUS_Addr var, uint16_t len, const int16_t* boxSize, bool center) {
  if (len == 0)     {
    return;
  }
  DEBUG_ECHOLNPAIR_F("setsize len ", len);
  // set text size and box size based on filename length
  int boxWidth = boxSize[2];
  int maxCharWidth = boxWidth / len;
  int fontWidth = _MIN(maxCharWidth, 10);
  uint16_t fontSize = Swap16((fontWidth << 8) | (fontWidth * 2));
  DEBUG_ECHOLNPAIR_F("boxwidth ", boxWidth, " maxcharwidth ", maxCharWidth, " fontwidth ", fontWidth);

  dgus_display.Write((uint16_t)var + (int)DGUS_SP_Text::FONT_SIZE, fontSize);

  // adjust box y and height
  int height = fontWidth * 2;
  int heightDiff = (height - boxSize[3]) / 2;
  int widthDiff = (fontWidth * (int)len - boxSize[2]) / 2;
  DEBUG_ECHOLNPAIR_F("heightdiff ", heightDiff, " widthdiff ", widthDiff);
  uint16_t box[4];
  box[0] = Swap16(boxSize[0] - (center ? widthDiff : 0));
  box[1] = Swap16(boxSize[1] - heightDiff);
  box[2] = Swap16(boxSize[2] + boxSize[0] - 1 + (center ? widthDiff : 0));
  box[3] = Swap16(boxSize[3] + boxSize[1] - 1 + heightDiff);
  DEBUG_ECHOLNPAIR_F("xs ", boxSize[0] - (center ? widthDiff : 0), " ys ", boxSize[1] - heightDiff, " xe ", boxSize[2] + boxSize[0]-1 +  (center ? widthDiff : 0)," ye ", boxSize[3] + boxSize[1] - 1 + heightDiff);
  dgus_display.Write((uint16_t)var + (int)DGUS_SP_Text::BOX, box, 4*sizeof(uint16_t));

  uint16_t xy[2];
  xy[0] = box[0];
  xy[1] = box[1];
  dgus_display.Write((uint16_t)var + (int)DGUS_SP_Text::X, xy, sizeof(xy));
}

void DGUSScreenHandler::ShowWaitScreen(DGUS_Screen return_screen, bool has_continue) {
  if (return_screen != DGUS_Screen::WAIT)     {
    wait_return_screen = return_screen;
  }
  wait_continue = has_continue;

  TriggerScreenChange(DGUS_Screen::WAIT);
}

DGUS_Screen DGUSScreenHandler::GetCurrentScreen() {
  return current_screen;
}

void DGUSScreenHandler::TriggerScreenChange(DGUS_Screen screen) {
  new_screen = screen;
}

void DGUSScreenHandler::TriggerFullUpdate() {
  full_update = true;
}

void DGUSScreenHandler::TriggerEEPROMSave() {
  eeprom_save = ExtUI::safe_millis() + 500;
}

bool DGUSScreenHandler::IsPrinterIdle() {
  return (!ExtUI::commandsInQueue() && !ExtUI::isMoving());
}

const DGUS_Addr* DGUSScreenHandler::FindScreenAddrList(DGUS_Screen screen) {
  DGUS_ScreenAddrList list;
  const DGUS_ScreenAddrList* map = screen_addr_list_map;

  do     {
    memcpy_P(&list, map, sizeof(*map));
    if (!list.addr_list)
      break;
    if (list.screen == screen)         {
      return list.addr_list;
    }
  } while (++map);

  return nullptr;
}

bool DGUSScreenHandler::CallScreenSetup(DGUS_Screen screen) {
  DGUS_ScreenSetup setup;
  const DGUS_ScreenSetup* list = screen_setup_list;

  do     {
    memcpy_P(&setup, list, sizeof(*list));
    if (!setup.setup_fn)
      break;
    if (setup.screen == screen)         {
      return setup.setup_fn();
    }
  } while (++list);

  return true;
}

void DGUSScreenHandler::MoveToScreen(DGUS_Screen screen, bool abort_wait) {
  if (current_screen == DGUS_Screen::KILL)     {
    return;
  }

  if (current_screen == DGUS_Screen::WAIT)     {
    if (screen != DGUS_Screen::WAIT)         {
      wait_return_screen = screen;
    }

    if (!abort_wait)
      return;

    if (wait_continue && wait_for_user)         {
      ExtUI::setUserConfirmed();
    }
  }

  if (!CallScreenSetup(screen))
    return;

  if (!SendScreenVPData(screen, true))     {
    DEBUG_ECHOLNPGM("SendScreenVPData failed");
    return;
  }

  DEBUG_ECHOLNPAIR_F("From screen ", (uint16_t)current_screen, " to screen ", (uint16_t)screen);
  current_screen = screen;
  dgus_display.SwitchScreen(current_screen);
}

bool DGUSScreenHandler::SendScreenVPData(DGUS_Screen screen, bool complete_update) {
  if (complete_update)     {
    full_update = false;
  }

  const DGUS_Addr* list = FindScreenAddrList(screen);

  while (true)     {
    if (!list)
      return true; // Nothing left to send

    const uint16_t addr = pgm_read_word(list++);
    if (!addr)
      return true; // Nothing left to send

    DGUS_VP vp;
    if (!DGUS_PopulateVP((DGUS_Addr)addr, &vp))
      continue; // Invalid VP
    if (!vp.tx_handler)
      continue; // Nothing to send
    if (!complete_update && !(vp.flags & VPFLAG_AUTOUPLOAD))
      continue; // Unnecessary VP

    uint8_t expected_tx = 6 + vp.size; // 6 bytes header + payload.
    const millis_t try_until = ExtUI::safe_millis() + 1000;

    while (expected_tx > dgus_display.GetFreeTxBuffer())         {
      if (ELAPSED(ExtUI::safe_millis(), try_until))
        return false; // Stop trying after 1 second

      dgus_display.FlushTx(); // Flush the TX buffer
      delay(50);
    }

    vp.tx_handler(vp);
  }
}
#endif // DGUS_LCD_UI_RELOADED
