/*
requires:
libgstreamer-plugins-base0.10-dev libgtk2.0-dev

installation:
sudo apt-get install libgstreamer-plugins-base0.10-dev
sudo apt-get install libgtk2.0-dev
*/

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include "video_send.c"
#include "sk18comm.c"
#include "rs232.h"
#include "rs232.c"
#include "crcds.c"
#include <time.h>

int okres = 5;
/* mniej więcej co ile milisekund powtarzana jest pętla główna */
int failsafe = 520;
/* po jakim czasie (w milisekundach) od ostatniego sygnału osi Y pojazd ma się zatrzymywać   */
/* wartość musi być nieco większa od częstotliwości powtarzania zdarzeń osi Y przez nadajnik */

struct timeval tv;
char czas_tekstowo[30];
time_t czas_start, czas_stop; /* czas w sekundach od początku epoki */
long unsigned int czas_start_mikro, czas_stop_mikro; /* czas w mikrosekundach od ostatniej sekundy */
long unsigned int czas_milisekund_stop, czas_milisekund_start; /* czas w milisekundach od początku ostatniego okresu 11 dniowego */
/* koniec fragmentu potrzebnego do mierzenia czasu */

int wychylenie_x = 500;
int wychylenie_y = 500;

#define POZ_NEUTRALNA 100
int predkosc;

struct sockaddr_in sa;
unsigned char buffer[1024];
size_t fromlen, recsize;
int sock;

GIOChannel *iochan;
GError *sockerror;

int comport_relayboard = 16;
int comport_sk = 17;
char mode[]={'8','N','2'};
/* serwokontroler i regulator są na początku działania programu domyślnie włączone */
gboolean stan_serwokontroler = TRUE;
gboolean stan_regulator = TRUE;
gboolean kierunek = TRUE;

int zamknij() {
	gtk_main_quit();
	return 0;
}

void relay_switch(char stan, int numer) {
	/* jeśli karta przekaźników/serwokontroler jest niepodłączony to program wyświetla niepotrzebne znaki! */
	if (access(comports[comport_relayboard], F_OK) == 0) {
		uint8_t crctab[] = { 0x01, stan, numer };
		uint8_t crc = CountCRC(0x00, crctab, 3);
		uint8_t buff[] = { 0x55, 0x01, stan, numer, crc };
		RS232_SendBuf(comport_relayboard, buff, 5);
	}
}

void wychyl_x(int wychylenie_x) {
	printf("◄ ► %5hd\n", wychylenie_x);
	/* printf("synchronizacja:%4hd, number:%4hd, posHigh:%4hd, posLow/speed:%4hd\n", packet[0],packet[1],packet[2],packet[3]); */
	if (access(comports[comport_sk], F_OK) == 0) {
		CalculatePacket(1, wychylenie_x, 8);
		RS232_SendBuf(comport_sk, packet, 4);
	}
}

void wychyl_y(int wychylenie_y) {

	/* tutaj można dodać opcję sprintu - regulator umożliwia osiągnięcia pełnej prędkości przez krótki czas */

	gboolean kierunek_przed;
	kierunek_przed = kierunek;

	/* trzy przedziały wychylenia joysticka w osi Y */
	if (wychylenie_y <= 490) {
		predkosc = ((-(wychylenie_y - 501)) * 0.5) + POZ_NEUTRALNA;
		kierunek = TRUE; /* naprzód */
	}

	if (wychylenie_y > 490 && wychylenie_y <= 510) {
		/* w tym obszarze wychyleń osi Y pojazd będzie stał w miejscu */
		predkosc = POZ_NEUTRALNA;
	}

	if (wychylenie_y > 510) {
		predkosc = ((wychylenie_y - 501) * 0.5) + POZ_NEUTRALNA;
		kierunek = FALSE; /* wstecz */
	}

	/* eliminuje skutki drgań drążka w okolicach pozycji neutralnej */
	if (predkosc < 4 && predkosc > -4) {
		predkosc = 4;
	}

	if (kierunek != kierunek_przed) {
		/* jeśli zaszła zmiana kierunku */
		/* przekaźnik 0x00 to zasilanie serwokontrolera			*/
		/* przekaźniki 0x01 i 0x02 to jazda do przodu i do tyłu	*/
		if (kierunek) {
			printf("zmiana kierunku ▲\n");
			relay_switch('F', 0x02);
			relay_switch('O', 0x01);
		}
		if (!kierunek) {
			printf("zmiana kierunku ▼\n");
			relay_switch('F', 0x01);
			relay_switch('O', 0x02);
		}
	} else {/* z jakiegoś niezrozumiałego powodu ten else musi tutaj być */
		printf("\r");
	}

	/* zapisywanie czasu zdarzenia dotyczącego osi Y 		*/
	gettimeofday(&tv, NULL);
	czas_start = tv.tv_sec;
	czas_start_mikro = tv.tv_usec;
	czas_milisekund_start = (tv.tv_sec % 1000000) * 1000 + tv.tv_usec / 1000;
	/* przelicza sekundy i mikrosekundy na milisekundy */
	/* koniec zapisywania czasu zdarzenia dotyczącego osi Y */
}

int pwm_send() {
	if (stan_regulator == TRUE) {
		/* zabezpieczenie przed wyjechaniem poza zasięg			*/
		if (predkosc != POZ_NEUTRALNA) {
			gettimeofday(&tv, NULL);
			czas_stop = tv.tv_sec;
			czas_stop_mikro = tv.tv_usec;
			// strftime(czas_tekstowo,30,"%m-%d-%Y  %T.",localtime(&czas_stop));
			// printf("%s (%ld sekund od epoki) %ld mikrosekund\n",czas_tekstowo,tv.tv_sec,tv.tv_usec);
			czas_milisekund_stop = (tv.tv_sec % 1000000) * 1000
					+ tv.tv_usec / 1000; /* przelicza sekundy i mikrosekundy na milisekundy */

			if (czas_milisekund_stop > czas_milisekund_start + failsafe) {
				/* jeśli od ostatniego zdarzenia osi Y (prędkość) minęło więcej czasu niż mija zwykle */
				strftime(czas_tekstowo, 30, "%m-%d-%Y  %T.",
						localtime(&czas_start));
				printf(
						"Czas ostatniego zdarzenia osi Y: %s\n%ld sekund od epoki %ld mikrosekund\nZatrzymanie!\n",
						czas_tekstowo, tv.tv_sec, tv.tv_usec);

				predkosc = POZ_NEUTRALNA;
				kierunek = TRUE;
				relay_switch('F', 0x02);
				relay_switch('O', 0x01);
				/* nie wyłącza przekaźnika żeby nie trzeba go było włączać po wznowieniu połączenia */
			}
		}
		/* koniec zabezpieczenia przed wyjechaniem poza zasięg */

		if (access(comports[comport_sk], F_OK) == 0) {
			CalculatePacket(3, predkosc, 4);
			RS232_SendBuf(comport_sk, packet, 4);
		}
		/* printf("synchronizacja:%4hd, number:%4hd, posHigh:%4hd, posLow/speed:%4hd\n", packet[0],packet[1],packet[2],packet[3]); */
	}

	else {
		return FALSE;
	}
}

/* PWM - pulse width modulation */
void pwm_on() {
	/* włącza okresowe wysyłanie sygnału do regulatora prędkości */
	stan_regulator = TRUE;
	g_timeout_add(50, pwm_send, NULL);
	/* powtarzaj funkcję co zadaną liczbę milisekund */
	/* regulator obrotów powinien otrzymywać impuls PWM z częstotliwością 2 KHz
	 ale otrzymuje tylko co 50 milisekund czyli z częstotliwością 0,5 KHz */
	if (access(comports[comport_relayboard], F_OK) == 0) {
		printf("regulator załączony\n");
		relay_switch('O', 0x01);
		relay_switch('F', 0x02);
	}
}

void pwm_off() {
	stan_regulator = FALSE;
	printf("regulator wyłączony\n");
	relay_switch('F', 0x01);
	relay_switch('F', 0x02);
}

void zinterpretuj_dwustan(unsigned char buffer[1024]) {
	/* interpretuje zdarzenia dotyczące przycisków */

	int number = buffer[0] / 2 - 1;
	int value = buffer[0] % 2;

	printf("przycisk: %2hd\tstan:%2hd\n", number, value);

	if (buffer[0] == 27) /* 12, 1 */
	{
		printf("kod przycisku: %2hd ", buffer[0]); /* bajt identyfikujący polecenie włączenia kamery (27) */
		v4l_device_number = buffer[1]; /* numer kamery (0,1...) */
		printf("numer kamery: %d ", v4l_device_number);
		sprintf(nadajnik_IP_string, "%d.%d.%d.%d", buffer[2], buffer[3],
				buffer[4], buffer[5]); /* numer IP (0-255. 0-255. 0-255. 0-255) */
		printf("adres IP nadajnika: %s\n", nadajnik_IP_string);
		if (video_running == FALSE) {
			video_start();
			audio_start();
		}
	}
	if (buffer[0] == 26 && video_running == TRUE) /* 12, 0 */
	{
		printf("kod przycisku: %2hd\n", buffer[0]);
		video_stop();
		audio_stop();
	}

	if (number == 14 && value == 0) {
		stan_serwokontroler = FALSE;
		/* wyłącza zasilanie serwokontrolera */
		relay_switch('F', 0x00);
		printf("◅ ▻ Zasilanie serwokontrolera wyłączone.\n");
		/* zamyka port szeregowy serwokontrolera */
		RS232_CloseComport(comport_sk);
		printf("Port serwokontrolera zamknięty.\n");
	}

	if (number == 14 && value == 1) {
		stan_serwokontroler = TRUE;
		/* załącza zasilanie serwokontrolera */
		relay_switch('O', 0x00);
		printf("◄ ► Zasilanie serwokontrolera załączone.\n");
		/* otwiera port szeregowy serwokontrolera */
		RS232_OpenComport(comport_sk, 9600, mode);
		printf("port szeregowy serwokontrolera %s numer %2hd\n",
				comports[comport_sk], comport_sk);
		/* ustawia koła w pozycji środkowej */
		if (access(comports[comport_sk], F_OK) == 0) {
			CalculatePacket(1, 500, 4);
			RS232_SendBuf(comport_sk, packet, 4);
		}
		printf("Koła ustawione w pozycji środkowej.\n");
	}

	if (number == 8 && value == 1) {
		printf("wyłącz silnik\n");
		pwm_off();
	}

	if (number == 9 && value == 1) {
		/* prędkość na jakąś do przodu */
		printf("załącz silnik\n");
		pwm_on();
	}

	if (number == 13 && value == 1) {
		/* wyłącza odbiornik (serwer) */
		zamknij();
	}
}

void zdarzenie() {
	recsize = recvfrom(sock, (void *) buffer, 1024, 0, (struct sockaddr *) &sa,
			&fromlen);
	if (recsize < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
	}

	printf("odebrano: %2hd▽: ", recsize);
	/* printf("\t %2hd\t %2hd\t %2hd\t %2hd\t %2hd\n", buffer[0],buffer[1],buffer[2],buffer[3],buffer[4]); */

	/* jeśli pierwszy bajt jest mniejszy lub równy 64 to jest to sygnał przycisku (dyskretny) */
	if (buffer[0] <= 64) {
		/* wartość bufora podzielona przez dwa i pomniejszona przez jeden to numer przycisku	*/
		/* przesunięcie o 1 dlatego że nie można (było) wysłać bajtu wynoszącego 0				*/
		/* reszta z dzielenia to stan przycisku: 1 lub 0										*/
		/* parzyste zwolnienie, nieparzyste wciśnięcie 											*/
		zinterpretuj_dwustan(buffer);
	}

	/* jeśli pierwszy bajt jest większy od 64 to jest to sygnał drążka (liczbowy) */
	if (buffer[0] > 64) {
		/* wyliczanie wychylenia serwomechanizmu				*/

		/* printf("numer osi:%2hd\twychylenie:%6hd\n", (buffer[0]-128)/16, (buffer[0]%16)*64 + buffer[1]-128 ); */
		if ((buffer[0] - 128) / 16 == 0)
		/* jeśli dotyczy osi X */
		{
			wychylenie_x = (buffer[0] % 16) * 64 + (buffer[1] - 128);
			wychyl_x(wychylenie_x);
		}

		if ((buffer[0] - 128) / 16 == 1)
		/* jeśli dotyczy osi Y */
		{
			wychylenie_y = (buffer[0] % 16) * 64 + (buffer[1] - 128);
			wychyl_y(wychylenie_y);

			/* dla pewności wysyła prędkość pomimo że funkcja pwm_send robi to w regularnych odstępach czasu */
			/*
			 if (access(comports[comport_sk], F_OK) == 0)
			 {CalculatePacket(3, predkosc, 4);
			 RS232_SendBuf(comport_sk, packet, 4);}
			 */
		}
	}
}

void otworz_UDP() {
	/* potrzebne do otwierania gniazda */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htons(7654);
	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	/* koniec fragment potrzebnego do otwierania gniazda */

	if (-1 == bind(sock, (struct sockaddr *) &sa, sizeof(struct sockaddr))) {
		perror("error bind failed");
		close(sock);
		exit(EXIT_FAILURE);
	}

	iochan = g_io_channel_unix_new(sock);
	int iowatch = -1;
	/* FIXME: "warning: passing argument 3 of ‘g_io_add_watch’ from incompatible pointer type /usr/include/glib-2.0/glib/giochannel.h:199: 
	 note: expected ‘GIOFunc’ but argument is of type ‘void (*)()’" */
	iowatch = g_io_add_watch(iochan, G_IO_IN, zdarzenie, NULL);
	printf("Port UDP otwarty\n");
}

void zamknij_UDP() {
	/* conflicting types */
	g_io_channel_flush(iochan, &sockerror);
	g_io_channel_shutdown(iochan, TRUE, &sockerror);
	g_io_channel_unref(iochan);
	printf("Port UDP zamknięty\n");
	close(sock);
}

void otworz_urzadzenia() {
	/* sprawdza obecność karty przekaźników, kontrolera serwomechanizmów i otwiera ich porty */

	/*
	 user must be a member of following groups: dialout, uucp:
	 sudo adduser username dialout
	 sudo adduser username uucp
	 */
	int nr_portu;
	for (nr_portu = 16; nr_portu <= 21; nr_portu++) {
		if (access(comports[nr_portu], F_OK) != 0) {
			printf("urządzenie %s niepodłączone\n", comports[nr_portu]);
		} else {
			printf("urządzenie %s to: ", comports[nr_portu]);
			unsigned char *readbuf;
			readbuf[0] = 0;
			readbuf[1] = 0;
			readbuf[2] = 0;
			readbuf[3] = 0;
			readbuf[4] = 0;

			/* sprawdza czy urządzenie jest kartą przekaźników */
			//TODO: not sure if mode for relay board is the same as for servo controller
			RS232_OpenComport(nr_portu, 57600, mode);
			/* wysyła polecenie Get */
			/*	uint8_t crctab[] = {0x01, 'G', 0x01};
			 uint8_t crc = CountCRC(0x00, crctab,3);
			 uint8_t buff[] = {0x55, crctab[0], crctab[1], crctab[2], crc};
			 */
			uint8_t buff[] = { 0x55, 0x01, 0x47, 0x00, 0x5e };
			RS232_SendBuf(nr_portu, buff, 5);

			/* czeka dwie sekundy na odpowiedź karty przekaźników */
			usleep(20000);
			/*
			 na pakiet Get urządzenie odpowiada w sposób:
			 0x55 0x01 'R' XX CRC8 - XX to wartość wyjść w formacie binarnym.
			 */

			/* odbiera i interpretuje to co karta wysłała */
			read(Cport[nr_portu], readbuf, 5);
			/* printf("%X %2X %X %X %X\n", readbuf[0], readbuf[1], readbuf[2], readbuf[3], readbuf[4]); */

			if (readbuf[0] == 0x55) {
				/* jeśli pierwszy bajt bufora jest zgodny z protokołem karty przekaźników */
				printf("karta przekaźników\n");
				/* DODAĆ drukowanie aktualnych stanów wszystkich przekaźników
				 readbuf[3] jako pole bitowe (bitfield) */
				comport_relayboard = nr_portu;
			} else {
				/* jeśli nie jest zgodny zakładamy że to urządzenie jest serwokontrolerem	*/
				/* kontroler serwomechanizmów nie odpowiada bo niema takiej możliwości		*/
				printf("kontroler serwomechanizmów\n");
				comport_sk = nr_portu;
				/* otwiera port szeregowy serwokontrolera */
				RS232_OpenComport(comport_sk, 9600, mode);
			}
		}
	}

	/* załącza zasilanie serwokontrolera (przekaźnik numer 0x00) */
	relay_switch('O', 0x00);
	/* jeśli serwokontroler jest zasilany z głównego akumulatora poprzez regulator obrotów
	 (funkcja BEC), to można sobie darować ten fragment, bo będzie pod napięciem cały czas */
}

int main(int argc, char *argv[]) {
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);

	otworz_UDP();
	otworz_urzadzenia();

	/* zaczyna wysyłać sygnały do regulatora */
	predkosc = POZ_NEUTRALNA;
	pwm_on();
	/*
	 gtk_signal_connect(NULL,"key_press_event", G_CALLBACK(klawisz), 1);
	 gtk_signal_connect(NULL,"key_release_event", G_CALLBACK(klawisz), 0);
	 */
	gtk_main();
	zamknij_UDP();
	/* TODO: zerowanie osi wyłączanie przekaźników zamykanie portów szeregowych */

	return 0;
	/* jest też w funkcji zamknij */
}
