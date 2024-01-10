import sys
from PyQt5.QtWidgets import QApplication, QDialog, QWidget, QMessageBox
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QMutex, QMutexLocker, QEventLoop
from PyQt5.QtCore import QTimer
import socket
import random
from time import sleep

# CO IDZIE DO SERVERA
# + -> +1 punkt dla gracza
# - -> -1 szansa
# w -> wygranie rundy +10 dla gracza
# f -> przegranie gry - TODO

# CO IDZIE DO GRACZA
# ok -> gracz przyjenty do gry
# no -> gracz nieprzyjety do gry - pokoj pelny lub inny problem
# s -> zaczela sie nowa gra
# zaraz po s przychodza nicknames uzytkownikow


# - -> info ze gracz o jakims id stracil szanse
# nickname -> przychodzi po "-" info ktoremu graczowi odjec pkt

# n -> nowa runda (ktos wygral runde)
# e -> koniec gry 
# w -> klient wygral gre


sys.path.append('/StartDialogUI.py')
from StartDialogUI import Ui_StartDialog

sys.path.append('/HangmanGameScreen2.py')
from HangmanGameScreen2 import Ui_GameScreen

sys.path.append('/createNewGame.py')
from createNewGame import Ui_CreateNewGameUI

sys.path.append('/WaitingRoom.py')
from WaitingRoom import Ui_Dialog

BUFFER_SIZE = 1024

# TODO:
# changing number of players in game creating
PLAYERS = 2
MAX_PLAYERS = 5

class GameWindow(QWidget, Ui_GameScreen):
    def __init__(self, connection_thread, client_socket, players_nicknames):
        super(GameWindow, self).__init__()
        self.setupUi(self)
        self.word_to_guess = ""
        self.current_word_state = ""
        self.attempts_left_per_game = 7
        self.used_letters = set()
        self.round = 0
        self.total_score = 0
        self.connection_thread = connection_thread
        self.client_socket = client_socket
        self.players_leaderboard = {}
        self.players_in_gui = {}

        players_list = players_nicknames.strip().split()
        for nickname in players_list:
            self.players_leaderboard[nickname] = 0

        self.set_players_info_gui(players_list)
     

    def connect_signals(self, connection_thread):
            connection_thread.signal_round_start.connect(self.start_new_round)
            print("Connected signal_round_start to start_new_round")
  
            connection_thread.signal_game_end.connect(self.handle_game_over)
            connection_thread.signal_update_hangman.connect(self.update_hangman)
    
    def set_players_info_gui(self, players_nicknames):
        nicknames_label = [self.playerNickname1, self.playerNickname2,
                            self.playerNickname3, self.playerNickname4,
                            self.playerNickname5]
        hangmans_labels = [self.playerHangman1, self.playerHangman2,
                            self.playerHangman3, self.playerHangman4,
                            self.playerHangman5]
                

        for nickname_label, hangman_label, nickname in zip(nicknames_label[:PLAYERS], hangmans_labels[:PLAYERS], players_nicknames):
            nickname_label.setText(nickname)
            self.players_in_gui[nickname] = [nickname_label, hangman_label]

        if PLAYERS < 5:
            for nickname_label, hangman_label in zip(nicknames_label[PLAYERS:], hangmans_labels[PLAYERS:]):
                nickname_label.setText("")
                hangman_label.setStyleSheet("image: none;")


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
                        print("sending: +")
                        self.client_socket.sendall("+\n".encode('utf-8'))
                    else:
                        new_word_state += current_state_letter

                self.current_word_state = new_word_state
                self.update_word_label()

                if "_" not in self.current_word_state:
                    print("sending: w")
                    self.client_socket.sendall("w\n".encode('utf-8'))
            else:
                self.attempts_left_per_game -= 1
                self.update_attempts_label()
                print("sending: -")
                self.client_socket.sendall("-\n".encode('utf-8'))

    
                if self.attempts_left_per_game == 0:
                    
                    # TODO:
                    # fail signal sending
                    self.client_socket.sendall("f\n".encode('utf-8'))
                    self.handle_game_over("You ran out of attempts for the entire game")

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

    def update_hangman(self, nickname, chances):
        print(nickname)
        paths = [":/hangmans/hangman1.png",":/hangmans/hangman2.png",
                 ":/hangmans/hangman3.png", ":/hangmans/hangman4.png",
                 ":/hangmans/hangman5.png",":/hangmans/hangman6.png",
                 ":/hangmans/hangman7.png", ":/hangmans/hangman8.png"]
    
        self.players_in_gui[nickname][1].setStyleSheet(f"image: url({paths[-chances-1]});")


    def handle_game_over(self, message):
        QMessageBox.information(self, "Game Over", message, QMessageBox.Ok)
        self.round = 0
        self.total_score = 0
        self.attempts_left_per_game = 7
        self.connection_thread.close_connection()
        self.close()
        startDialog.gameIdEdit.setText("")
        startDialog.show()



class ConnectionThread(QThread):
    signal_game_start = pyqtSignal(str)
    signal_round_start = pyqtSignal(str)
    signal_game_end = pyqtSignal(str)
    signal_update_hangman = pyqtSignal(str, int)
    

    def __init__(self, nick_name, game_id):
        super(ConnectionThread, self).__init__()
        self.nick_name = nick_name
        self.game_id = game_id
        self.client_socket = None
        self.mutex = QMutex()
        self.mutex.lock()



    def run(self):
        try:
            server_address = ("192.168.122.1", 2000)
            self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.client_socket.connect(server_address)

            if self.game_id == 0:
                self.game_id = 69
                print("gameis is 69")

            message = f"{self.nick_name} {self.game_id}" 
            self.client_socket.sendall(message.encode('utf-8'))



            self.handle_server_updates() 

            # chunk = ""
            # response, chunk = self.split_serv_msg(chunk)

            # if response == "ok":
            #     print("Joined game.")
            #     self.handle_server_updates() 

            # elif response == "no":
            #     print("Full room.")

            # else:
            #     print("Unexpected message from the server:", response)

        except Exception as e:
            print("Error connecting to server:", e)

    def close_connection(self):
        try:
            if self.client_socket:
                self.client_socket.close()
        except Exception as e:
            print("Error closing connection:", e)

    def split_serv_msg(self, chunk):
        msg = ""
        if chunk == "":
            chunk = self.client_socket.recv(BUFFER_SIZE).decode('utf-8')
            print("chunk:", chunk)

        newline_pos = chunk.find('\n')
        msg += chunk[:newline_pos]
        print("msg after split: ", msg, "*")
        if newline_pos != -1:
            chunk = chunk[newline_pos+1:]
        if newline_pos == -1:
            chunk = ""
    
        return msg, chunk
                

    
    def handle_server_updates(self):
        chunk = ""
        mess = ""
        while True:
            mess, chunk = self.split_serv_msg(chunk)

            # TODO przy create new game id=0 i wtedy serwer musi zwracac gameid jakie ma byc
            if self.game_id == 0:
                # TODO:
                # self.game_id = mess - po zwroceniu wolnego game id przez srv
                pass

            elif mess == "ok":
                print("Joined game.")
                self.waitingRoomDialog = WaitingRoomDialog()
                self.waitingRoomDialog.show()

            elif mess == "no":
                QMessageBox.information(self, "You cannot join", "room is full!", QMessageBox.Ok)

            elif mess == "s":
                self.waitingRoomDialog.hide()
                players_nicknames, chunk = self.split_serv_msg(chunk)
                print(players_nicknames)
                self.signal_game_start.emit(players_nicknames)
                self.mutex.lock()

            elif mess == "n":
                secret_word, chunk = self.split_serv_msg(chunk)
                print("secret word", secret_word)
                self.signal_round_start.emit(secret_word)
                
            elif mess == "e":
                self.signal_game_end.emit("Theres no more rounds to play!")


            elif mess == "-":
                player_info, chunk = self.split_serv_msg(chunk)
                player_info = player_info.split()
                player_nickname = player_info[0]
                player_chances = player_info[1]     
                print(player_nickname)
                self.signal_update_hangman.emit(player_nickname, int(player_chances))
            elif mess == "w":
                print("you jusssssssst won enttttttttire gaaaaame!!!")
                QMessageBox.information(self, "Game Over", "You win!", QMessageBox.Ok)# nie dzialaaaaaaaa
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
        if not game_id or not game_id.isdigit():
            QMessageBox.warning(self, "Input Error", "Please enter a valid game ID (numeric).", QMessageBox.Ok)
            return
        if not nick_name:
            QMessageBox.warning(self, "Input Error", "Please enter nickname.", QMessageBox.Ok)
            return
        elif " " in nick_name:
            QMessageBox.warning(self, "Input Error", "Nickname cannot contain SPACES!.", QMessageBox.Ok)
            return
        
        try:
            self.hide()
            self.connection_thread = ConnectionThread(nick_name, game_id)
            self.connection_thread.start()
            self.connect_signals(self.connection_thread)
        except Exception as e:
            QMessageBox.warning(self, "Connection Error", f"Error connecting to server: {e}", QMessageBox.Ok)
            self.show()

    def onCreateGameButtonClicked(self):
        nick_name = self.nickNameEdit.text()
        if not nick_name:
            QMessageBox.warning(self, "Input Error", "Please enter nickname.", QMessageBox.Ok)
            return
        else:
            self.hide()
            self.createNewGameDialog = CreateNewGameDialog(nick_name, self)
            self.createNewGameDialog.activateWindow()
            self.createNewGameDialog.raise_()
            self.createNewGameDialog.show()




    def start_new_game(self, players_nicknames):
        self.hide()
        self.game_window = GameWindow(self.connection_thread, self.connection_thread.client_socket, players_nicknames)
       
        self.game_window.connect_signals(self.connection_thread)  
        self.connection_thread.mutex.unlock()

        self.game_window.activateWindow()
        self.game_window.raise_()
        self.game_window.show()

class CreateNewGameDialog(QDialog, Ui_CreateNewGameUI):
    def __init__(self, nickname, startDialog):
        super(CreateNewGameDialog, self).__init__()
        self.setupUi(self)
        self.createGameButton.clicked.connect(self.onCreateGameButtonClicked)
        self.nickname = nickname
        self.startDialog = startDialog

    def onCreateGameButtonClicked(self):

        try:
            self.hide()
            startDialog.connection_thread = ConnectionThread(self.nickname, 0)
            # TODO: send  max rounds and players count to connthread
            startDialog.connection_thread.start()
            startDialog.connect_signals(startDialog.connection_thread)
            
        except Exception as e:
            QMessageBox.warning(self, "Connection Error", f"Error connecting to server: {e}", QMessageBox.Ok)
            startDialog.show()

class WaitingRoomDialog(QDialog, Ui_Dialog):
    def __init__(self):
        super(WaitingRoomDialog, self).__init__()
        self.setupUi(self)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    startDialog = StartDialog()
    startDialog.show()


    sys.exit(app.exec_())