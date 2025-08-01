import requests
import mysql.connector
from mysql.connector import errorcode
from bs4 import BeautifulSoup
import os
from dotenv import load_dotenv

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
API_PATH = os.path.join(SCRIPT_DIR, "apis.env")
load_dotenv(API_PATH) 
apiKey = os.getenv('apiKey')
if not apiKey:
    raise ValueError("API key is missing. Please set it in the .env file.")

# TradingView scanner API endpoint
# url = "https://companiesmarketcap.com/"

# Send POST request
# response = requests.post(url)

# data = response.json()

# for stock in data['data']:
#    name = stock['s']
#    idx = name.find(':')
#    name = name[idx+1:]
#    print(name)


popular_stocks = [
    'AAPL', 'MSFT', 'AMZN', 'GOOGL', 'AMD',
    'NVDA', 'BRK.B', 'META', 'TSLA', 'UNH',
    'JNJ', 'JPM', 'V', 'PG', 'MA',
    'HD', 'XOM', 'AVGO', 'KO', 'PEP'
]


try:
    connection = mysql.connector.connect(user='root', password='',
                              host='127.0.0.1',
                              database='Trading')

except mysql.connector.Error as err:
  if err.errno == errorcode.ER_ACCESS_DENIED_ERROR:
    print("Something is wrong with your user name or password")
  elif err.errno == errorcode.ER_BAD_DB_ERROR:
    print("Database does not exist")
  else:
    print(err)
else:
  print('Connection successful')

cursor = connection.cursor()    

for symbol in popular_stocks:
    alphaUrl = 'https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol='+symbol+'&apikey='+ apiKey
    r = requests.get(alphaUrl)
    data = r.json()
    price =  data['Global Quote']['02. open'] 
    price = float(price)
    r = requests.get('https://ticker-2e1ica8b9.now.sh/keyword/' + symbol)
    data = r.json()
    for element in data:
        if element['symbol'] == symbol:
            name = element['name']
            break

    addStock = ("INSERT INTO stocks (Symbol, CompanyName, StockPrice) "
                "VALUES (%s, %s, %s) "
                "ON DUPLICATE KEY UPDATE "
                "CompanyName = VALUES(CompanyName), "
                "StockPrice = VALUES(StockPrice)")
    stockData= (symbol, name, price)

    cursor.execute(addStock, stockData)

connection.commit()
cursor.close()
connection.close()