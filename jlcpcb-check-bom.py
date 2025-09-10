import csv
import sys

def filtra_csv(input_file, output_file):
    """
    Filtra un file CSV, mantenendo solo le righe con un valore non vuoto
    nella colonna 'LCSC'. La prima riga (intestazione) viene sempre mantenuta.
    """
    try:
        with open(input_file, mode='r', newline='', encoding='utf-8') as infile:
            reader = csv.DictReader(infile)
            fieldnames = reader.fieldnames
            
            # Se la colonna non esiste, solleva un errore
            if "LCSC" not in fieldnames:
                print("Errore: La colonna 'LCSC' non è stata trovata nel file.")
                return

            filtered_rows = []
            # Itera su ogni riga del file CSV
            for row in reader:
                # Controlla se la colonna 'LCSC' non è vuota
                if row.get("LCSC"):
                    filtered_rows.append(row)

        # Scrivi le righe filtrate in un nuovo file CSV
        with open(output_file, mode='w', newline='', encoding='utf-8') as outfile:
            writer = csv.DictWriter(outfile, fieldnames=fieldnames)
            
            # Scrive l'intestazione
            writer.writeheader()
            
            # Scrive le righe filtrate
            writer.writerows(filtered_rows)
        
        print(f"Operazione completata con successo! Il nuovo file è stato salvato come '{output_file}'.")
        print(f"Sono state trovate e salvate {len(filtered_rows)} righe con la colonna 'LCSC' presente.")

    except FileNotFoundError:
        print(f"Errore: Il file '{input_file}' non è stato trovato.")
    except Exception as e:
        print(f"Si è verificato un errore: {e}")

# Questa parte gestisce gli argomenti da riga di comando
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Uso: python filtra_csv.py <file_di_input.csv> <file_di_output.csv>")
    else:
        input_csv = sys.argv[1]
        output_csv = sys.argv[2]
        filtra_csv(input_csv, output_csv)
