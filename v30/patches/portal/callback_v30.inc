static void v29EmergencyPortalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(MESH_OFFGRIDNL_V30)
  static uint32_t armed = 0, armedAt = 0;
  const uint32_t pending = v29_emergency_portal.pendingApproval();
  if (pending) {
    if (g_web_mirror.clients() || g_web_mirror.termClients() || g_web_mirror.remote()) {
      armed=0;
      if (g_lv.task) g_lv.task->showAlert("Sluit eerst de externe bediening.", 2500);
      return;
    }
    if (armed == pending && (uint32_t)(millis()-armedAt)<10000u) {
      const bool accepted=v29_emergency_portal.approvePending(pending);
      armed=0;
      if (g_lv.task) g_lv.task->showAlert(accepted ? "Lokaal bevestigd. Ontvangst nog onbevestigd." : "Aanvraag verlopen.", 2500);
      return;
    }
    armed=pending; armedAt=millis();
    const auto a=v30::PortalApproval::action(pending);
    const char* label=a==v30::PortalRequest::Help ? "Hulpvraag" : a==v30::PortalRequest::Safe ? "Veilig" : a==v30::PortalRequest::Moving ? "Onderweg" : "Verzamelpunt";
    char msg[112];
    snprintf(msg,sizeof(msg),"%s: druk binnen 10 seconden opnieuw op Telefoonportaal om te bevestigen.",label);
    if (g_lv.task) g_lv.task->showAlert(msg,5000);
    return;
  }
  armed=0;
#endif
  v29PortalStartApply();
}
