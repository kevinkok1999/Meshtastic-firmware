static void v29EmergencyPortalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  v29PortalStartApply();
}
