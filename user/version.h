#ifdef MC_66B
#	ifdef THERMO_NO
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "MC-B-THERMO_NO-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "MC-B-THERMO_NO"
#		endif	// THERMO_ON_AC_2
#	else	// THERMO_NC
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "MC-B-THERMO_NC-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "MC-B-THERMO_NC"
#		endif	// THERMO_ON_AC_2
#	endif
#elif defined(EN61107)
#	ifdef THERMO_NO
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "MC-THERMO_NO-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "MC-THERMO_NO"
#		endif	// THERMO_ON_AC_2
#	else	// THERMO_NC
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "MC-THERMO_NC-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "MC-THERMO_NC"
#		endif	// THERMO_ON_AC_2
#	endif
#elif defined(IMPULSE)
#		define HW_MODEL "IMPULSE"
#elif defined(DEBUG_NO_METER)
#		define HW_MODEL "NO_METER"
#else
#	ifdef THERMO_NO
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "THERMO_NO-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "THERMO_NO"
#		endif	// THERMO_ON_AC_2
#	else	// THERMO_NC
#		ifdef THERMO_ON_AC_2
#			define HW_MODEL "THERMO_NC-THERMO_ON_AC_2"
#		else
#			define HW_MODEL "THERMO_NC"
#		endif	// THERMO_ON_AC_2
#	endif
#endif

