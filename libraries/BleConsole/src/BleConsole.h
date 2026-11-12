/*
 * Console texte par BLE (service « GUS » de Goodix, UUID a6ed0201-…), pour le développement sur une
 * carte sans port série. Lignes terminées par CR ou LF.
 *
 * Commandes intégrées : help, info, reboot, dfu (retour au bootloader). L'application peut en ajouter
 * avec onCommand(). Tout ce qui est écrit sur Serial est aussi envoyé sur la console.
 *
 * Si advertise = true, la console annonce elle-même l'appareil (nom + service GUS). Sinon elle se
 * contente d'ajouter son service : à combiner avec une autre application BLE qui gère les annonces.
 */
#pragma once

#include <Arduino.h>

class BleConsoleClass : public Print {
public:
  typedef bool (*CommandHandler)(int argc, char **argv);   /* true si la commande est reconnue */

  void begin(const char *deviceName, const char *version = "", bool advertise = true);
  void poll();                                   /* à appeler depuis loop() */
  void onCommand(CommandHandler h) { handler = h; }
  bool connected() const;
  uint16_t lastError() const;                    /* première erreur BLE rencontrée (0 = aucune) */

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override;
  using Print::write;

private:
  CommandHandler handler = nullptr;
};

extern BleConsoleClass BleConsole;
