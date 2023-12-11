import sys
from PyQt5.QtWidgets import QApplication, QDialog, QWidget, QMessageBox
from PyQt5.QtCore import Qt, QThread, pyqtSignal
import socket
import random
from time import sleep

sys.path.append('/StartDialogUI.py')
from StartDialogUI import Ui_StartDialog

sys.path.append('/HangmanGameScreen.py')
from HangmanGameScreen import Ui_GameScreen


class GameWindow(QWidget, Ui_GameScreen):
    def __init__(self):
        super(GameWindow, self).__init__()
        self.setupUi(self)
        self.word_to_guess = ""
        self.current_word_state = ""
        self.attempts_left_per_game = 6 
        self.used_letters = set()
        self.round = 0
        self.total_score = 0

        self.start_new_game()

    def keyPressEvent(self, event):
        key = event.text().upper()
        if key.isalpha() and key not in self.used_letters:
            self.used_letters.add(key)

            if key in self.word_to_guess:
                new_word_state = ""
                for word_letter, current_state_letter in zip(self.word_to_guess, self.current_word_state):
                    if word_letter == key:
                        new_word_state += key
                    else:
                        new_word_state += current_state_letter

                self.current_word_state = new_word_state
                self.update_word_label()

                if "_" not in self.current_word_state:
                    self.handle_round_over("Congratulations! You won this round!")
                    # Wygral info do servera
            else:
                self.attempts_left_per_game -= 1
                self.update_attempts_label()

    
                if self.attempts_left_per_game == 0:
                    self.handle_game_over("Game over! You ran out of attempts for the entire game")

        self.update_used_letters_label()

    def start_new_game(self):
        if self.round < 5:
            # Initialize or reset round variables
            self.word_to_guess = self.generate_random_word().upper()
            self.current_word_state = "_" * len(self.word_to_guess)
            self.used_letters = set()

            # Update UI elements
            self.update_word_label()
            self.update_attempts_label()
            self.update_used_letters_label()

            # Increment round number
            self.round += 1
        else:
            # End of the game, display final score
            self.handle_game_over(f"Game over! Your final score is {self.total_score}")

    def generate_random_word(self):
        # Replace this with your own logic to fetch words from a database or file
        word_list = ["PYTHON", "JAVA", "CPLUSPLUS", "JAVASCRIPT", "HTML", "CSS"]
        return random.choice(word_list)

    def update_word_label(self):
        # Update the word label with the current state of the word
        self.wordLabel.setText(" ".join(self.current_word_state))

    def update_attempts_label(self):
        self.attemptsLabel.setText(f"Attempts left: {self.attempts_left_per_game}")

    def update_used_letters_label(self):
        used_letters_text = "Used Letters: " + ", ".join(sorted(self.used_letters))
        self.usedLettersLabel.setText(used_letters_text)

    def handle_round_over(self, message):
        # Show a message box with the round result
        QMessageBox.information(self, "Round Over", message, QMessageBox.Ok)

        round_score = self.attempts_left_per_game

        self.total_score += round_score

        self.start_new_game()

    def handle_game_over(self, message):
        # Show a message box with the game result
        QMessageBox.information(self, "Game Over", message, QMessageBox.Ok)

        # Ask the user if they want to play again
        reply = QMessageBox.question(self, "Play Again?", "Do you want to play again?", QMessageBox.Yes | QMessageBox.No)
        if reply == QMessageBox.Yes:
            self.round = 0
            self.total_score = 0
            self.attempts_left_per_game = 6
            self.start_new_game()
        else:
            self.close()
            startDialog.show()

class ConnectionThread(QThread):
    signal_game_start = pyqtSignal()

    def __init__(self, nick_name, game_id):
        super(ConnectionThread, self).__init__()
        self.nick_name = nick_name
        self.game_id = game_id

    def run(self):
        try:
            server_address = ('localhost', 1234)
            self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.client_socket.connect(server_address)

            message = f"{self.nick_name} {self.game_id}"
            self.client_socket.sendall(message.encode('utf-8'))

            # Wait for a signal from the server indicating that the game is ready to start
            response = self.client_socket.recv(1024).decode('utf-8')

            if response == "GameStart":
                print("All players connected. Starting the game.")
                self.signal_game_start.emit()

            else:
                print("Unexpected message from the server:", response)
                # Handle unexpected message if needed

        except Exception as e:
            print("Error connecting to server:", e)


class StartDialog(QDialog, Ui_StartDialog):
    def __init__(self):
        super(StartDialog, self).__init__()
        self.setupUi(self)
        self.joinGameButton.clicked.connect(self.onJoinGameButtonClicked)
        self.client_socket = None 
        self.connection_thread = None

    def onJoinGameButtonClicked(self):
        game_id = self.gameIdEdit.text()
        nick_name = self.nickNameEdit.text()
        print("Clicked Join Game Button. Game ID:", game_id, "Nick Name:", nick_name)

        try:
            
            self.connection_thread = ConnectionThread(nick_name, game_id)
            self.connection_thread.signal_game_start.connect(self.onGameStart)
            self.connection_thread.start()
        except Exception as e:
            QMessageBox.warning(self, "Connection Error", f"Error connecting to server: {e}", QMessageBox.Ok)

    # def connect_to_server(self, nick_name, game_id):
    #     server_address = ('localhost', 1234)
    #     self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    #     self.client_socket.connect(server_address)

    #     self.client_socket.sendall(f"{nick_name}".encode('utf-8'))

    #     self.client_socket.sendall(f"{game_id}".encode("utf-8"))

    def onGameStart(self):
        # This method is called when the game is ready to start
        # Hide the current dialog and show the game window
        self.hide()
        self.game_window = GameWindow()
        self.game_window.show()
        
    
   
if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    startDialog = StartDialog()
    startDialog.show()

    sys.exit(app.exec_())
