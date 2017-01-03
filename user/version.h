#ifdef MC_66B
#	ifdef THERMO_NO
#		define HW_MODEL "MC-B-THERMO_NO"
#	else	// THERMO_NC
#		define HW_MODEL "MC-B-THERMO_NC"
#	endif
#elif EN61107
#	ifdef THERMO_NO
#		define HW_MODEL "MC-THERMO_NO"
#	else	// THERMO_NC
#		define HW_MODEL "MC-THERMO_NC"
#	endif
#elif defined IMPULSE
#		define HW_MODEL "IMPULSE"
#else
#	ifdef THERMO_NO
#		define HW_MODEL "THERMO_NO"
#	else	// THERMO_NC
#		define HW_MODEL "THERMO_NC"
#	endif
#endif

