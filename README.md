Portail CAME Carte de commande pour controle le portail

Liste des composants lien

Template allumage extinction

template:
  #Bouton + lumière Telerupteur
  - light:
      - name: "ECL_Couloir"
        unique_id: ecl_couloir
        state: >
          {{ is_state('binary_sensor.ecl_couloir_zone_ias', 'off') }}
        turn_off:
          service: switch.toggle
          target:
            entity_id: switch.ecl_couloir
        turn_on:
          service: switch.toggle
          target:
            entity_id: switch.ecl_couloir

Reste à faire en fonction des demandes:

  - Création d'une boite à imprimer en 3D
  - Quirk ZHA
  - Mise à jour par OTA

Posibilité d'assemblée pour vous une carte prête à l'emploi.
