#pragma once

namespace xpx::mod
{
    // Solo per le prove (mai nel processo della barra delle applicazioni): se nella cartella dei dati esiste
    // dev-exceptions.txt, ogni eccezione C++ e ogni eccezione grave del processo va nel log della mod con lo stack
    // (modulo+offset, da tradurre con i simboli). Serve a capire una chiusura anomala (abort, fail fast).
    void InstallDevExceptionLog(bool shellProcess);
    // Scaricamento della mod: toglie il gestore (il suo codice sta nella DLL).
    void RemoveDevExceptionLog();
}
