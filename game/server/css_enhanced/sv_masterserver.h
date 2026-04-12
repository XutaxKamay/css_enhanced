//=============================================================================//
//
// Purpose: Server-side masterserver HTTPS API client.
//          Handles querying the masterserver for player information
//          such as default clan tags via token identification.
//
//=============================================================================//

#ifndef SV_MASTERSERVER_H
#define SV_MASTERSERVER_H

#ifdef CSTRIKE_DLL

// Request the default clan tag for a player from the masterserver.
// The request is asynchronous - results are queued and applied via ProcessResponses.
void MasterServer_RequestDefaultClanTag( int playerIndex, const char *token );

// Process any completed masterserver responses and apply them.
// Should be called once per server frame (e.g., from CCSGameRules::Think).
void MasterServer_ProcessResponses();

#else

inline void MasterServer_RequestDefaultClanTag( int playerIndex, const char *token ) {}
inline void MasterServer_ProcessResponses() {}

#endif // CSTRIKE_DLL

#endif // SV_MASTERSERVER_H
