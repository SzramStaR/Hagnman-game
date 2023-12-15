import sys
from PyQt5.QtWidgets import QApplication, QDialog, QWidget, QMessageBox
from PyQt5.QtCore import Qt, QThread, pyqtSignal
from PyQt5.QtCore import QTimer
import socket
import random
from time import sleep
# CO IDZIE DO SERVERA
# + -> +1 punkt dla gracza
# - -> -1 szansa
# w -> wygranie rundy +10 dla gracza

# CO IDZIE DO GRACZA
# ok -> gracz przyjenty do gry
# no -> gracz nieprzyjety do gry - pokoj pelny lub inny problem
# s -> zaczela sie nowa gra

# - -> info ze gracz o jakims id stracil szanse
# nickname -> przychodzi po "-" info ktoremu graczowi odjec pkt

# n -> nowa runda (ktos wygral runde)
# e -> koniec gry 

sys.path.append('/StartDialogUI.py')
from StartDialogUI import Ui_StartDialog

sys.path.append('/HangmanGameScreen.py')
from HangmanGameScreen import Ui_GameScreen

BUFFER_SIZE = 1024

class GameWindow(QWidget, Ui_GameScreen):
    def __init__(self, connection_thread, client_socket, players_nicknames):
        super(GameWindow, self).__init__()
        self.setupUi(self)
        self.word_to_guess = ""
        self.current_word_state = ""
        self.attempts_left_per_game = 6 
        self.used_letters = set()
        self.round = 0
        self.total_score = 0
        self.connection_thread = connection_thread
        self.client_socket = client_socket
        self.players_leaderboard = {}

        players_list = players_nicknames.strip().split()
        for nickname in players_list:
            self.players_leaderboard[nickname] = 0
     

    def connect_signals(self, connection_thread):
            connection_thread.signal_round_start.connect(self.start_new_round)
            print("Connected signal_round_start to start_new_round")
  
            connection_thread.signal_game_end.connect(self.handle_game_over)
            connection_thread.signal_update_hangman.connect(self.update_hangman)

    def splitString(string):
        return string.strip().split()

    def keyPressEvent(self, event):
        key = event.text().upper()
        if key.isalpha() and key not in self.used_letters:
            self.used_letters.add(key)

            if key in self.word_to_guess:
                new_word_state = ""
                for word_letter, current_state_letter in zip(self.word_to_guess, self.current_word_state):
                    if word_letter == key:
                        new_word_state += key
                        self.client_socket.sendall("+\n".encode('utf-8'))
                    else:
                        new_word_state += current_state_letter

                self.current_word_state = new_word_state
                self.update_word_label()

                if "_" not in self.current_word_state:
                    
                    self.client_socket.sendall("w\n".encode('utf-8'))
            else:
                self.attempts_left_per_game -= 1
                self.update_attempts_label()
                self.client_socket.sendall("-\n".encode('utf-8'))

    
                if self.attempts_left_per_game == 0:
                    self.handle_game_over("Game over! You ran out of attempts for the entire game")

        self.update_used_letters_label()

    def handle_game_start(self):
        print("init game..")
    
    def start_new_round(self, secret_word):
        print(f"Received signal_round_start with secret_word: {secret_word}")
        

        self.word_to_guess = secret_word.upper()
        self.current_word_state = "_" * len(self.word_to_guess)
        self.used_letters = set()


        self.update_word_label()
        self.update_attempts_label()
        self.update_used_letters_label()

    def update_word_label(self):
        self.wordLabel.setText(" ".join(self.current_word_state))

    def update_attempts_label(self):
        self.attemptsLabel.setText(f"Attempts left: {self.attempts_left_per_game}")

    def update_used_letters_label(self):
        used_letters_text = "Used Letters: " + ", ".join(sorted(self.used_letters))
        self.usedLettersLabel.setText(used_letters_text)

    def update_hangman(self, nickname):
        print(nickname)

    def handle_game_over(self, message):
        QMessageBox.information(self, "Game Over", message, QMessageBox.Ok)

        reply = QMessageBox.question(self, "Play Again?", "Do you want to play again?", QMessageBox.Yes | QMessageBox.No)
        if reply == QMessageBox.Yes:
            self.round = 0
            self.total_score = 0
            self.attempts_left_per_game = 6
            # TO DO:
            # wyczyścić pola do wpisania id gry i nickname w StartDialog
        else:
            self.close()
            startDialog.show()

class ConnectionThread(QThread):
    signal_game_start = pyqtSignal(str)
    # sygnal co wysyla slowo
    signal_round_start = pyqtSignal(str)
    signal_game_end = pyqtSignal()
    signal_update_hangman = pyqtSignal(str)
    

    def __init__(self, nick_name, game_id):
        super(ConnectionThread, self).__init__()
        self.nick_name = nick_name
        self.game_id = game_id
        self.client_socket = None

    def run(self):
        try:
            server_address = ('localhost', 2000)
            self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.client_socket.connect(server_address)

            message = f"{self.nick_name} {self.game_id}"
            self.client_socket.sendall(message.encode('utf-8'))

            response = self.recv_serv_msg()

            if response == "ok":
                print("Joined game.")
                self.handle_server_updates() 

            elif response == "no":
                print("Full room.")

            else:
                print("Unexpected message from the server:", response)


        except Exception as e:
            print("Error connecting to server:", e)

    def recv_serv_msg(self):
        msg =""
        while True:
            chunk = self.client_socket.recv(BUFFER_SIZE).decode('utf-8')
            msg+= chunk
            if '\n' in msg:
                msg = msg.strip('\n')
                break
        print("fn gives: ", msg)
        return msg

    
    def handle_server_updates(self):
        while(1):
            mess = self.recv_serv_msg()
            if mess == "s":
                players_nicknames = self.recv_serv_msg()
                print(players_nicknames)
                self.signal_game_start.emit(players_nicknames)

            elif mess == "n":
                secret_word = self.recv_serv_msg()
                print("secret word" ,secret_word)
                self.signal_round_start.emit(secret_word)
        
            elif mess == "e":
                self.signal_game_end.emit()
            
            elif mess == "-":
                player_nickname = self.recv_serv_msg()
                print(player_nickname)
                self.signal_update_hangman.emit(player_nickname)
                
            else:
                print("Unexpected message from the server:", mess, " ", len(mess))     




class StartDialog(QDialog, Ui_StartDialog):
    def __init__(self):
        super(StartDialog, self).__init__()
        self.setupUi(self)
        self.joinGameButton.clicked.connect(self.onJoinGameButtonClicked)
        self.createGameButton.clicked.connect(self.onCreateGameButtonClicked)
        self.client_socket = None 
        self.connection_thread = None

    def connect_signals(self, connection_thread):
        connection_thread.signal_game_start.connect(self.start_new_game)

    def onJoinGameButtonClicked(self):
        game_id = self.gameIdEdit.text()
        nick_name = self.nickNameEdit.text()
        print("Clicked Join Game Button. Game ID:", game_id, "Nick Name:", nick_name)

        try:
            self.connection_thread = ConnectionThread(nick_name, game_id)
            self.connection_thread.start()
            
            self.connect_signals(self.connection_thread)
        except Exception as e:
            QMessageBox.warning(self, "Connection Error", f"Error connecting to server: {e}", QMessageBox.Ok)

    def onCreateGameButtonClicked(self):
        nick_name = self.nickNameEdit.text()
        # TO DO:
        # pobranie game_id wolnego z serwera przez conn thread



    def start_new_game(self, players_nicknames):
        self.hide()
        self.game_window = GameWindow(self.connection_thread, self.connection_thread.client_socket, players_nicknames)
       
        self.game_window.connect_signals(self.connection_thread)  


        self.game_window.activateWindow()
        self.game_window.raise_()
        self.game_window.show()

    
   
if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    startDialog = StartDialog()
    startDialog.show()

    sys.exit(app.exec_())