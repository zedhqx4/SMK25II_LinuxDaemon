#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <alsa/asoundlib.h>

// ============================================================================
// VARIABLES GLOBALES (1 Cliente, 5 Puertos Paralelos)
// ============================================================================
// Manejador principal para la interfaz del secuenciador ALSA MIDI.
static snd_seq_t *manejador_secuenciador = NULL;

// Identificador único asignado por el sistema para nuestro cliente ALSA.
static int identificador_cliente_propio = -1;

// --- VÍA 1: Hardware -> DAW (Programa 1) ---
static int puerto_entrada_hardware        = -1; // Recibe los eventos MIDI crudos del teclado físico.
static int puerto_salida_notas_hacia_daw  = -1; // Reenvía las notas musicales limpias hacia el DAW.
static int puerto_salida_mcu_hacia_daw    = -1; // Traduce y envía comandos Mackie Control (MCU) al DAW.

// --- VÍA 2: DAW -> Hardware (Programa 2) ---
static int puerto_entrada_feedback_daw    = -1; // Recibe el feedback o datos de retorno desde el DAW (Omni-Pass).
static int puerto_salida_hacia_hardware   = -1; // Reenvía de forma transparente los mensajes de vuelta hacia el teclado físico.

// --- Tracking ---
static int hardware_conectado = 0;             // Bandera de estado para saber si el teclado está vinculado.
static int identificador_cliente_hardware = -1; // ID del cliente ALSA perteneciente al teclado físico detectado.
static int puerto_salida_fisica_teclado   = -1; // Puerto físico de salida del teclado (fuente de datos).
static int puerto_entrada_fisica_teclado  = -1; // Puerto físico de entrada del teclado (destino de feedback).

// ============================================================================
// LÓGICA VÍA 1: TRADUCCIÓN (Programa 1)
// ============================================================================
// Escala un valor de control deslizante de 7 bits (0-127) a un rango completo de alta resolución de 14 bits (0-16383).
static void escalar_posicion_a_pitchbend_14bit(int posicion_7bit, int *byte_lsb, int *byte_msb) {
    int valor_14bit = (int)((posicion_7bit / 127.0) * 16383.0);
    *byte_lsb = valor_14bit & 0x7F;
    *byte_msb = (valor_14bit >> 7) & 0x7F;
}

// Envía un evento de Nota MIDI (Note On / Note Off) traducido a protocolo MCU hacia el DAW.
static void emitir_nota_mcu_al_daw(unsigned char canal, unsigned char nota, unsigned char velocidad) {
    snd_seq_event_t evento_midi;
    snd_seq_ev_clear(&evento_midi);
    snd_seq_ev_set_direct(&evento_midi);

    // Si la velocidad es mayor a cero es Note On, de lo contrario se interpreta como Note Off.
    if (velocidad > 0) {
        snd_seq_ev_set_noteon(&evento_midi, canal, nota, velocidad);
    } else {
        snd_seq_ev_set_noteoff(&evento_midi, canal, nota, 0);
    }

    // Configura el puerto de origen y despacha el evento de forma inmediata al subsistema.
    snd_seq_ev_set_source(&evento_midi, puerto_salida_mcu_hacia_daw);
    snd_seq_ev_set_subs(&evento_midi);
    snd_seq_event_output(manejador_secuenciador, &evento_midi);
}

// Envía un evento de Pitch Bend traducido a protocolo MCU hacia el DAW usando los bytes LSB y MSB.
static void emitir_pitchbend_mcu_al_daw(unsigned char canal, int lsb, int msb) {
    snd_seq_event_t evento_midi;
    snd_seq_ev_clear(&evento_midi);
    snd_seq_ev_set_direct(&evento_midi);

    // Combina los bytes de 14 bits y aplica el desplazamiento estándar centrado en 8192.
    int pitch_bend = (((int)msb << 7) | (int)lsb) - 8192;
    snd_seq_ev_set_pitchbend(&evento_midi, canal, pitch_bend);

    // Configura el puerto de origen y despacha el evento.
    snd_seq_ev_set_source(&evento_midi, puerto_salida_mcu_hacia_daw);
    snd_seq_ev_set_subs(&evento_midi);
    snd_seq_event_output(manejador_secuenciador, &evento_midi);
}

// Procesa y filtra los eventos entrantes desde el hardware en la Vía 1.
static void procesar_via_1_desde_hardware(const snd_seq_event_t *evento_entrante) {
    // 1. Reenvío de Notas Musicales: Si no es un mensaje SysEx, se copia y se redirige al puerto de notas del DAW.
    if (evento_entrante->type != SND_SEQ_EVENT_SYSEX) {
        snd_seq_event_t evento_copia = *evento_entrante;
        snd_seq_ev_set_direct(&evento_copia);
        snd_seq_ev_set_source(&evento_copia, puerto_salida_notas_hacia_daw);
        snd_seq_ev_set_subs(&evento_copia);
        snd_seq_event_output(manejador_secuenciador, &evento_copia);
        return;
    }

    // 2. Traducción de SysEx a MCU: Lee los bytes crudos y valida la cabecera propietaria del controlador.
    const unsigned char *datos_sysex = (const unsigned char *)evento_entrante->data.ext.ptr;
    size_t tamano_sysex = evento_entrante->data.ext.len;
    if (tamano_sysex < 6 || datos_sysex[0] != 0xF0 || datos_sysex[1] != 0x35 || datos_sysex[2] != 0x59) return;

    unsigned char control = datos_sysex[3];
    unsigned char valor   = datos_sysex[5];

    // Traduce mensajes de faders/controles deslizantes o botones especiales a comandos Mackie Control.
    if (control >= 0x60 && control <= 0x67) {
        int lsb, msb;
        escalar_posicion_a_pitchbend_14bit(valor, &lsb, &msb);
        emitir_pitchbend_mcu_al_daw(control - 0x60, lsb, msb);
    } else if (control == 0x10) {
        unsigned char identificador = datos_sysex[4];
        unsigned char velocidad = (valor == 0x7F) ? 127 : 0;
        if (identificador <= 0x07 || (identificador >= 0x68 && identificador <= 0x6F) || identificador == 0x4C) {
            emitir_nota_mcu_al_daw(9, (identificador == 0x4C) ? 81 : identificador, velocidad);
        } else {
            emitir_nota_mcu_al_daw(9, identificador, velocidad);
        }
    }
}

// ============================================================================
// LÓGICA VÍA 2: OMNI-PASS (Programa 2)
// ============================================================================
// Recibe el flujo de datos proveniente del DAW y lo retransmite de forma transparente al hardware.
static void procesar_via_2_desde_daw(const snd_seq_event_t *evento_entrante) {
    snd_seq_event_t evento_copia = *evento_entrante;

    // Modifica el origen del evento para que salga a través del puerto físico enlazado al teclado.
    snd_seq_ev_set_source(&evento_copia, puerto_salida_hacia_hardware);
    snd_seq_ev_set_subs(&evento_copia);
    snd_seq_ev_set_direct(&evento_copia);

    // Envía el evento de manera directa sin pasar por colas adicionales.
    snd_seq_event_output_direct(manejador_secuenciador, &evento_copia);
}

// ============================================================================
// AUTOCONEXIÓN DUAL
// ============================================================================
// Escanea los clientes activos en el sistema ALSA para encontrar y conectar automáticamente el controlador MIDI.
static int escanear_y_conectar_hardware() {
    snd_seq_client_info_t *info_cliente;
    snd_seq_port_info_t   *info_puerto;
    snd_seq_client_info_alloca(&info_cliente);
    snd_seq_port_info_alloca(&info_puerto);
    snd_seq_client_info_set_client(info_cliente, -1);

    // Itera a través de todos los clientes MIDI registrados en el sistema.
    while (snd_seq_query_next_client(manejador_secuenciador, info_cliente) >= 0) {
        int cliente_evaluado = snd_seq_client_info_get_client(info_cliente);
        if (cliente_evaluado == identificador_cliente_propio) continue;

        const char *nombre_cliente = snd_seq_client_info_get_name(info_cliente);
        // Busca coincidencias con el nombre del teclado físico omitiendo puertos virtuales previos.
        if (nombre_cliente && strstr(nombre_cliente, "SMK25II") && !strstr(nombre_cliente, "Virtual")) {
            snd_seq_port_info_set_client(info_puerto, cliente_evaluado);
            snd_seq_port_info_set_port(info_puerto, -1);

            int puerto_in = -1, puerto_out = -1;
            // Itera por los puertos del cliente encontrado para identificar sus capacidades de E/S.
            while (snd_seq_query_next_port(manejador_secuenciador, info_puerto) >= 0) {
                unsigned int capacidades = snd_seq_port_info_get_capability(info_puerto);
                if ((capacidades & SND_SEQ_PORT_CAP_READ) && (capacidades & SND_SEQ_PORT_CAP_SUBS_READ)) puerto_out = snd_seq_port_info_get_port(info_puerto);
                if ((capacidades & SND_SEQ_PORT_CAP_WRITE) && (capacidades & SND_SEQ_PORT_CAP_SUBS_READ)) puerto_in = snd_seq_port_info_get_port(info_puerto);
            }

            // Si se detectan ambos puertos válidos, procede a realizar la suscripción bidireccional.
            if (puerto_in != -1 && puerto_out != -1) {
                snd_seq_port_subscribe_t *suscripcion;
                snd_seq_port_subscribe_alloca(&suscripcion);

                // Auto-Conexión VÍA 1: Establece la suscripción del Teclado Físico hacia nuestro programa.
                snd_seq_addr_t origen_via1 = { cliente_evaluado, puerto_out };
                snd_seq_addr_t destino_via1 = { identificador_cliente_propio, puerto_entrada_hardware };
                snd_seq_port_subscribe_set_sender(suscripcion, &origen_via1);
                snd_seq_port_subscribe_set_dest(suscripcion, &destino_via1);
                snd_seq_subscribe_port(manejador_secuenciador, suscripcion);

                // Auto-Conexión VÍA 2: Establece la suscripción de nuestro programa de vuelta hacia el Teclado Físico.
                snd_seq_addr_t origen_via2 = { identificador_cliente_propio, puerto_salida_hacia_hardware };
                snd_seq_addr_t destino_via2 = { cliente_evaluado, puerto_in };
                snd_seq_port_subscribe_set_sender(suscripcion, &origen_via2);
                snd_seq_port_subscribe_set_dest(suscripcion, &destino_via2);
                snd_seq_subscribe_port(manejador_secuenciador, suscripcion);

                // Guarda las referencias del estado de conexión actual.
                identificador_cliente_hardware = cliente_evaluado;
                puerto_entrada_fisica_teclado = puerto_in;
                puerto_salida_fisica_teclado = puerto_out;
                return 1;
            }
        }
    }
    return 0;
}

// ============================================================================
// MAIN LOOP PARALELO
// ============================================================================
int main() {
    // Abre el secuenciador ALSA en modo dúplex y no bloqueante (esencial para procesamiento asíncrono).
    if (snd_seq_open(&manejador_secuenciador, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) return 1;

    // Obtiene el ID asignado y configura el nombre visible del cliente MIDI en el sistema.
    identificador_cliente_propio = snd_seq_client_id(manejador_secuenciador);
    snd_seq_set_client_name(manejador_secuenciador, "SMK25II-Tool");

    // Definición de permisos básicos para las conexiones de puertos.
    unsigned int permisos_entrada = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
    unsigned int permisos_salida  = SND_SEQ_PORT_CAP_READ  | SND_SEQ_PORT_CAP_SUBS_READ;

    // Atributos de puerto optimizados para visibilidad simple en interfaces gráficas o parches de nodos. Para Que solo sea visible en PatchBay
    unsigned int tipo_puerto_grafos_nodos = SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION;

    // Atributos avanzados de puerto requeridos para que ALSA/PipeWire los categorice como hardware y sintetizador.
    unsigned int tipo_puerto_alsa = SND_SEQ_PORT_TYPE_MIDI_GENERIC |
    SND_SEQ_PORT_TYPE_SYNTH      |
    SND_SEQ_PORT_TYPE_HARDWARE;

    // Creación de Puertos VÍA 1 (Hardware -> Programa -> DAW)
    puerto_entrada_hardware       = snd_seq_create_simple_port(manejador_secuenciador, "HW_Input", permisos_entrada, tipo_puerto_grafos_nodos);
    puerto_salida_notas_hacia_daw = snd_seq_create_simple_port(manejador_secuenciador, "SMK25ii-bt", permisos_salida, tipo_puerto_alsa);
    puerto_salida_mcu_hacia_daw   = snd_seq_create_simple_port(manejador_secuenciador, "SMK25ii-bt-DAW", permisos_salida, tipo_puerto_alsa);

    // Creación de Puertos VÍA 2 (DAW -> Programa -> Hardware)
    puerto_entrada_feedback_daw   = snd_seq_create_simple_port(manejador_secuenciador, "SMK25ii-bt-DAW", permisos_entrada, tipo_puerto_alsa);
    puerto_salida_hacia_hardware  = snd_seq_create_simple_port(manejador_secuenciador, "HW_Output_MCP", permisos_salida, tipo_puerto_grafos_nodos);

    printf("Iniciado (ID: %d). Vía 1 y Vía 2 corriendo en paralelo.\n", identificador_cliente_propio);

    // Configura los descriptores de archivos para la supervisión de eventos mediante 'poll'.
    int cantidad_descriptores = snd_seq_poll_descriptors_count(manejador_secuenciador, POLLIN);
    struct pollfd *descriptores_poll = malloc(sizeof(struct pollfd) * cantidad_descriptores);

    while (1) {
        // Monitorea y gestiona la conexión/desconexión dinámica del dispositivo físico.
        if (!hardware_conectado) {
            if (escanear_y_conectar_hardware()) {
                hardware_conectado = 1;
                printf("Hardware SMK25II enlazado bidireccionalmente.\n");
            } else {
                sleep(1);
            }
        } else {
            // Verifica periódicamente si el cliente del hardware sigue activo en el sistema.
            snd_seq_client_info_t *info_cliente_hw;
            snd_seq_client_info_alloca(&info_cliente_hw);
            if (snd_seq_get_any_client_info(manejador_secuenciador, identificador_cliente_hardware, info_cliente_hw) < 0) {
                hardware_conectado = 0;
                printf("Hardware desconectado.\n");
            }
        }

        snd_seq_poll_descriptors(manejador_secuenciador, descriptores_poll, cantidad_descriptores, POLLIN);

        // Espera de forma eficiente eventos entrantes con un tiempo de espera de 200 ms.
        if (poll(descriptores_poll, cantidad_descriptores, 200) > 0) {
            snd_seq_event_t *evento_entrante;

            // Extrae y enruta de forma paralela los eventos según el puerto de destino.
            while (snd_seq_event_input(manejador_secuenciador, &evento_entrante) >= 0 && evento_entrante != NULL) {

                if (evento_entrante->dest.port == puerto_entrada_hardware) {
                    // El evento provino del teclado físico y se procesa en la VÍA 1.
                    procesar_via_1_desde_hardware(evento_entrante);
                }
                else if (evento_entrante->dest.port == puerto_entrada_feedback_daw) {
                    // El evento provino del DAW y se procesa en la VÍA 2.
                    procesar_via_2_desde_daw(evento_entrante);
                }

                snd_seq_free_event(evento_entrante);
            }
            // Drena el búfer de salida para despachar los eventos pendientes.
            snd_seq_drain_output(manejador_secuenciador);
        }
    }

    // Liberación de recursos antes de finalizar la ejecución.
    free(descriptores_poll);
    snd_seq_close(manejador_secuenciador);
    return 0;
}
