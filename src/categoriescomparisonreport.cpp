/***************************************************************************
 *   Copyright (C) 2006-2008, 2014, 2016 by Hanna Knutsson                 *
 *   hanna_k@fmgirl.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "categoriescomparisonreport.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMap>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QTextStream>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QUrl>
#include <QFileDialog>
#include <QSaveFile>
#include <QApplication>
#include <QTemporaryFile>
#include <QMimeDatabase>
#include <QMimeType>
#include <QDateEdit>
#include <QMessageBox>
#include <QPrinter>
#include <QTextEdit>
#include <QPrintDialog>
#include <QSettings>

#include "account.h"
#include "budget.h"
#include "recurrence.h"
#include "transaction.h"

#include <math.h>

extern QString htmlize_string(QString str);
extern double monthsBetweenDates(const QDate &date1, const QDate &date2);
extern double yearsBetweenDates(const QDate &date1, const QDate &date2);

CategoriesComparisonReport::CategoriesComparisonReport(Budget *budg, QWidget *parent, bool extra_parameters) : QWidget(parent), budget(budg), b_extra(extra_parameters) {

	setAttribute(Qt::WA_DeleteOnClose, true);

	QDate first_date;
	Transaction *trans = budget->transactions.first();
	while(trans) {
		if(trans->fromAccount()->type() != ACCOUNT_TYPE_ASSETS || trans->toAccount()->type() != ACCOUNT_TYPE_ASSETS) {
			first_date = trans->date();
			break;
		}
		trans = budget->transactions.next();
	}
	to_date = QDate::currentDate();
	from_date = to_date.addYears(-1).addDays(1);
	if(first_date.isNull()) {
		from_date = to_date;
	} else if(from_date < first_date) {
		from_date = first_date;
	}

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	QDialogButtonBox *buttons = new QDialogButtonBox(this);
	saveButton = buttons->addButton(tr("Save As…"), QDialogButtonBox::ActionRole);
	printButton = buttons->addButton(tr("Print…"), QDialogButtonBox::ActionRole);
	layout->addWidget(buttons);

	htmlview = new QTextEdit(this);
	htmlview->setReadOnly(true);
	layout->addWidget(htmlview);

	QSettings settings;
	settings.beginGroup("CategoriesComparisonReport");

	QGroupBox *settingsWidget = new QGroupBox(tr("Options"), this);
	QGridLayout *settingsLayout = new QGridLayout(settingsWidget);

	settingsLayout->addWidget(new QLabel(tr("Source:"), settingsWidget), 0, 0);
	QHBoxLayout *sourceLayout = new QHBoxLayout();
	settingsLayout->addLayout(sourceLayout, 0, 1);
	sourceCombo = new QComboBox(settingsWidget);
	sourceCombo->setEditable(false);
	sourceCombo->addItem(tr("All Categories"));
	Account *account = budget->expensesAccounts.first();
	while(account) {
		sourceCombo->addItem(tr("Expenses: %1").arg(account->name()));
		account = budget->expensesAccounts.next();
	}
	account = budget->incomesAccounts.first();
	while(account) {
		sourceCombo->addItem(tr("Incomes: %1").arg(account->name()));
		account = budget->incomesAccounts.next();
	}
	sourceLayout->addWidget(sourceCombo);

	payeeDescriptionWidget = NULL;
	if(b_extra) {
		payeeDescriptionWidget = new QWidget(settingsWidget);
		QHBoxLayout *payeeLayout = new QHBoxLayout(payeeDescriptionWidget);
		//payeeLayout->addWidget(new QLabel(tr("Divide on:"), payeeDescriptionWidget));
		QButtonGroup *group = new QButtonGroup(this);
		descriptionButton = new QRadioButton(tr("Descriptions for"), payeeDescriptionWidget);
		descriptionButton->setChecked(true);
		group->addButton(descriptionButton);
		payeeLayout->addWidget(descriptionButton);
		payeeCombo = new QComboBox(payeeDescriptionWidget);
		payeeCombo->setEditable(false);
		payeeLayout->addWidget(payeeCombo);
		payeeButton = new QRadioButton(tr("Payees/payers for"), payeeDescriptionWidget);
		group->addButton(payeeButton);
		payeeLayout->addWidget(payeeButton);
		descriptionCombo = new QComboBox(payeeDescriptionWidget);
		descriptionCombo->setEditable(false);
		payeeLayout->addWidget(descriptionCombo);
		sourceLayout->addWidget(payeeDescriptionWidget);
		payeeDescriptionWidget->setEnabled(false);
	} else {
		sourceLayout->addStretch(1);
	}

	current_account = NULL;
	has_empty_description = false;
	has_empty_payee = false;

	settingsLayout->addWidget(new QLabel(tr("Period:"), settingsWidget), 1, 0);
	QHBoxLayout *choicesLayout = new QHBoxLayout();
	settingsLayout->addLayout(choicesLayout, 1, 1);
	fromButton = new QCheckBox(tr("From"), settingsWidget);
	fromButton->setChecked(true);
	choicesLayout->addWidget(fromButton);
	fromEdit = new QDateEdit(settingsWidget);
	fromEdit->setCalendarPopup(true);
	fromEdit->setDate(from_date);
	choicesLayout->addWidget(fromEdit);
	choicesLayout->addWidget(new QLabel(tr("To"), settingsWidget));
	toEdit = new QDateEdit(settingsWidget);
	toEdit->setCalendarPopup(true);
	toEdit->setDate(to_date);
	choicesLayout->addWidget(toEdit);
	prevYearButton = new QPushButton(QIcon::fromTheme("eqonomize-previous-year"), "", settingsWidget);
	choicesLayout->addWidget(prevYearButton);
	prevMonthButton = new QPushButton(QIcon::fromTheme("eqonomize-previous-month"), "", settingsWidget);
	choicesLayout->addWidget(prevMonthButton);
	nextMonthButton = new QPushButton(QIcon::fromTheme("eqonomize-next-month"), "", settingsWidget);
	choicesLayout->addWidget(nextMonthButton);
	nextYearButton = new QPushButton(QIcon::fromTheme("eqonomize-next-year"), "", settingsWidget);
	choicesLayout->addWidget(nextYearButton);
	choicesLayout->addStretch(1);

	settingsLayout->addWidget(new QLabel(tr("Columns:"), settingsWidget), 2, 0);
	QHBoxLayout *enabledLayout = new QHBoxLayout();
	settingsLayout->addLayout(enabledLayout, 2, 1);
	valueButton = new QCheckBox(tr("Value"), settingsWidget);
	valueButton->setChecked(settings.value("valueEnabled", true).toBool());
	enabledLayout->addWidget(valueButton);
	dailyButton = new QCheckBox(tr("Daily"), settingsWidget);
	dailyButton->setChecked(settings.value("dailyAverageEnabled", true).toBool());
	enabledLayout->addWidget(dailyButton);
	monthlyButton = new QCheckBox(tr("Monthly"), settingsWidget);
	monthlyButton->setChecked(settings.value("monthlyAverageEnabled", true).toBool());
	enabledLayout->addWidget(monthlyButton);
	yearlyButton = new QCheckBox(tr("Yearly"), settingsWidget);
	yearlyButton->setChecked(settings.value("yearlyEnabled", false).toBool());
	enabledLayout->addWidget(yearlyButton);
	countButton = new QCheckBox(tr("Quantity"), settingsWidget);
	countButton->setChecked(settings.value("transactionCountEnabled", true).toBool());
	enabledLayout->addWidget(countButton);
	perButton = new QCheckBox(tr("Average value"), settingsWidget);
	perButton->setChecked(settings.value("valuePerTransactionEnabled", false).toBool());
	enabledLayout->addWidget(perButton);
	enabledLayout->addStretch(1);
	
	settings.endGroup();

	layout->addWidget(settingsWidget);

	if(b_extra) {
		connect(payeeButton, SIGNAL(toggled(bool)), this, SLOT(payeeToggled(bool)));
		connect(descriptionButton, SIGNAL(toggled(bool)), this, SLOT(descriptionToggled(bool)));
		connect(payeeCombo, SIGNAL(activated(int)), this, SLOT(payeeChanged(int)));
		connect(descriptionCombo, SIGNAL(activated(int)), this, SLOT(descriptionChanged(int)));
	}
	connect(sourceCombo, SIGNAL(activated(int)), this, SLOT(sourceChanged(int)));
	connect(valueButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(dailyButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(monthlyButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(yearlyButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(countButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(perButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(prevMonthButton, SIGNAL(clicked()), this, SLOT(prevMonth()));
	connect(nextMonthButton, SIGNAL(clicked()), this, SLOT(nextMonth()));
	connect(prevYearButton, SIGNAL(clicked()), this, SLOT(prevYear()));
	connect(nextYearButton, SIGNAL(clicked()), this, SLOT(nextYear()));
	connect(fromButton, SIGNAL(toggled(bool)), fromEdit, SLOT(setEnabled(bool)));
	connect(fromButton, SIGNAL(toggled(bool)), this, SLOT(updateDisplay()));
	connect(fromEdit, SIGNAL(dateChanged(const QDate&)), this, SLOT(fromChanged(const QDate&)));
	connect(toEdit, SIGNAL(dateChanged(const QDate&)), this, SLOT(toChanged(const QDate&)));
	connect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
	connect(printButton, SIGNAL(clicked()), this, SLOT(print()));

}

void CategoriesComparisonReport::payeeToggled(bool b) {
	if(b) updateDisplay();
}
void CategoriesComparisonReport::descriptionToggled(bool b) {
	if(b) updateDisplay();
}
void CategoriesComparisonReport::payeeChanged(int) {
	payeeButton->blockSignals(true);
	descriptionButton->blockSignals(true);
	descriptionButton->setChecked(true);
	payeeButton->setChecked(false);
	payeeButton->blockSignals(false);
	descriptionButton->blockSignals(false);
	updateDisplay();
}
void CategoriesComparisonReport::descriptionChanged(int) {
	payeeButton->blockSignals(true);
	descriptionButton->blockSignals(true);
	payeeButton->setChecked(true);
	descriptionButton->setChecked(false);
	payeeButton->blockSignals(false);
	descriptionButton->blockSignals(false);
	updateDisplay();
}
void CategoriesComparisonReport::sourceChanged(int i) {
	if(b_extra) {
		payeeCombo->blockSignals(true);
		descriptionCombo->blockSignals(true);
		payeeCombo->clear();
		descriptionCombo->clear();
		if(i == 0) {
			current_account = NULL;
			has_empty_description = false;
			has_empty_payee = false;
			payeeDescriptionWidget->setEnabled(false);
		} else {
			payeeDescriptionWidget->setEnabled(true);
			i--;
			if(i < (int) budget->expensesAccounts.count()) current_account = budget->expensesAccounts.at(i);
			else current_account = budget->incomesAccounts.at(i - budget->expensesAccounts.count());
			if(current_account) {
				descriptionCombo->addItem(tr("All descriptions"));
				if(current_account->type() == ACCOUNT_TYPE_EXPENSES) payeeCombo->addItem(tr("All payees"));
				else payeeCombo->addItem(tr("All payers"));
				has_empty_description = false;
				has_empty_payee = false;
				QMap<QString, bool> descriptions, payees;
				Transaction *trans = budget->transactions.first();
				while(trans) {
					if((trans->fromAccount() == current_account || trans->toAccount() == current_account)) {
						if(trans->description().isEmpty()) has_empty_description = true;
						else descriptions[trans->description()] = true;
						if(trans->type() == TRANSACTION_TYPE_EXPENSE) {
							if(((Expense*) trans)->payee().isEmpty()) has_empty_payee = true;
							else payees[((Expense*) trans)->payee()] = true;
						} else if(trans->type() == TRANSACTION_TYPE_INCOME) {
							if(((Income*) trans)->payer().isEmpty()) has_empty_payee = true;
							else payees[((Income*) trans)->payer()] = true;
						}
					}
					trans = budget->transactions.next();
				}
				QMap<QString, bool>::iterator it_e = descriptions.end();
				for(QMap<QString, bool>::iterator it = descriptions.begin(); it != it_e; ++it) {
					descriptionCombo->addItem(it.key());
				}
				if(has_empty_description) descriptionCombo->addItem(tr("No description"));
				QMap<QString, bool>::iterator it2_e = payees.end();
				for(QMap<QString, bool>::iterator it2 = payees.begin(); it2 != it2_e; ++it2) {
					payeeCombo->addItem(it2.key());
				}
				if(has_empty_payee) {
					if(current_account->type() == ACCOUNT_TYPE_EXPENSES) payeeCombo->addItem(tr("No payee"));
					else payeeCombo->addItem(tr("No payer"));
				}
			} else {
				payeeDescriptionWidget->setEnabled(false);
			}
		}
		payeeCombo->blockSignals(false);
		descriptionCombo->blockSignals(false);
	}
	updateDisplay();
}
void CategoriesComparisonReport::saveConfig() {
	QSettings settings;
	settings.beginGroup("CategoriesComparisonReport");
	settings.setValue("size", ((QDialog*) parent())->size());
	settings.setValue("valueEnabled", valueButton->isChecked());
	settings.setValue("dailyAverageEnabled", dailyButton->isChecked());
	settings.setValue("monthlyAverageEnabled", monthlyButton->isChecked());
	settings.setValue("yearlyAverageEnabled", yearlyButton->isChecked());
	settings.setValue("transactionCountEnabled", countButton->isChecked());
	settings.setValue("valuePerTransactionEnabled", perButton->isChecked());
	settings.endGroup();
}
void CategoriesComparisonReport::toChanged(const QDate &date) {
	bool error = false;
	if(!date.isValid()) {
		QMessageBox::critical(this, tr("Error"), tr("Invalid date."));
		error = true;
	}
	if(!error && fromEdit->date() > date) {
		if(fromButton->isChecked()) {
			QMessageBox::critical(this, tr("Error"), tr("To date is before from date."));
		}
		from_date = date;
		fromEdit->blockSignals(true);
		fromEdit->setDate(from_date);
		fromEdit->blockSignals(false);
	}
	if(error) {
		toEdit->setFocus();
		toEdit->blockSignals(true);
		toEdit->setDate(to_date);
		toEdit->blockSignals(false);
		toEdit->selectAll();
		return;
	}
	to_date = date;
	updateDisplay();
}
void CategoriesComparisonReport::fromChanged(const QDate &date) {
	bool error = false;
	if(!date.isValid()) {
		QMessageBox::critical(this, tr("Error"), tr("Invalid date."));
		error = true;
	}
	if(!error && date > toEdit->date()) {
		QMessageBox::critical(this, tr("Error"), tr("From date is after to date."));
		to_date = date;
		toEdit->blockSignals(true);
		toEdit->setDate(to_date);
		toEdit->blockSignals(false);
	}
	if(error) {
		fromEdit->setFocus();
		fromEdit->blockSignals(true);
		fromEdit->setDate(from_date);
		fromEdit->blockSignals(false);
		fromEdit->selectAll();
		return;
	}
	from_date = date;
	if(fromButton->isChecked()) updateDisplay();
}
void CategoriesComparisonReport::prevMonth() {
	fromEdit->blockSignals(true);
	toEdit->blockSignals(true);
	from_date = from_date.addMonths(-1);
	fromEdit->setDate(from_date);
	if(to_date.day() == to_date.daysInMonth()) {
		to_date = to_date.addMonths(-1);
		to_date.setDate(to_date.year(), to_date.month(), to_date.daysInMonth());
	} else {
		to_date = to_date.addMonths(-1);
	}
	toEdit->setDate(to_date);
	fromEdit->blockSignals(false);
	toEdit->blockSignals(false);
	updateDisplay();
}
void CategoriesComparisonReport::nextMonth() {
	fromEdit->blockSignals(true);
	toEdit->blockSignals(true);
	from_date = from_date.addMonths(1);
	fromEdit->setDate(from_date);
	if(to_date.day() == to_date.daysInMonth()) {
		to_date = to_date.addMonths(1);
		to_date.setDate(to_date.year(), to_date.month(), to_date.daysInMonth());
	} else {
		to_date = to_date.addMonths(1);
	}
	toEdit->setDate(to_date);
	fromEdit->blockSignals(false);
	toEdit->blockSignals(false);
	updateDisplay();
}
void CategoriesComparisonReport::prevYear() {
	fromEdit->blockSignals(true);
	toEdit->blockSignals(true);
	from_date = from_date.addYears(-1);
	fromEdit->setDate(from_date);
	if(to_date.day() == to_date.daysInMonth()) {
		to_date = to_date.addYears(-1);
		to_date.setDate(to_date.year(), to_date.month(), to_date.daysInMonth());
	} else {
		to_date = to_date.addYears(-1);
	}
	toEdit->setDate(to_date);
	fromEdit->blockSignals(false);
	toEdit->blockSignals(false);
	updateDisplay();
}
void CategoriesComparisonReport::nextYear() {
	fromEdit->blockSignals(true);
	toEdit->blockSignals(true);
	from_date = from_date.addYears(1);
	fromEdit->setDate(from_date);
	if(to_date.day() == to_date.daysInMonth()) {
		to_date = to_date.addYears(1);
		to_date.setDate(to_date.year(), to_date.month(), to_date.daysInMonth());
	} else {
		to_date = to_date.addYears(1);
	}
	toEdit->setDate(to_date);
	fromEdit->blockSignals(false);
	toEdit->blockSignals(false);
	updateDisplay();
}

void CategoriesComparisonReport::save() {
	QMimeDatabase db;
	QUrl url = QFileDialog::getSaveFileUrl(this, QString::null, QUrl(), db.mimeTypeForName("text/html").filterString());
	if(url.isEmpty() || !url.isValid()) return;
	QSaveFile ofile(url.toLocalFile());
	ofile.open(QIODevice::WriteOnly);
	ofile.setPermissions((QFile::Permissions) 0x0660);
	if(!ofile.isOpen()) {
		ofile.cancelWriting();
		QMessageBox::critical(this, tr("Error"), tr("Couldn't open file for writing."));
		return;
	}
	QTextStream outf(&ofile);
	outf.setCodec("UTF-8");
	outf << source;
	if(!ofile.commit()) {
		QMessageBox::critical(this, tr("Error"), tr("Error while writing file; file was not saved."));
		return;
	}
	
}

void CategoriesComparisonReport::print() {
	QPrinter printer;
	QPrintDialog print_dialog(&printer, this);
	if(print_dialog.exec() == QDialog::Accepted) {
		htmlview->print(&printer);
	}
}

void CategoriesComparisonReport::updateDisplay() {

	int columns = 2;
	bool enabled[6];
	enabled[0] = valueButton->isChecked();
	enabled[1] = dailyButton->isChecked();
	enabled[2] = monthlyButton->isChecked();
	enabled[3] = yearlyButton->isChecked();
	enabled[4] = countButton->isChecked();
	enabled[5] = perButton->isChecked();
	for(size_t i = 0; i < 6; i++) {
		if(enabled[i]) columns++;
	}

	QMap<Account*, double> values;
	QMap<Account*, double> counts;
	QMap<QString, double> desc_values;
	QMap<QString, double> desc_counts;
	double incomes = 0.0, costs = 0.0;
	double incomes_count = 0.0, costs_count = 0.0;
	double value_count = 0.0;
	double value = 0.0;

	current_account = NULL;
	current_description = "";
	current_payee = "";

	int i_source = 0;
	AccountType type = ACCOUNT_TYPE_EXPENSES;
	switch(sourceCombo->currentIndex()) {
		case 0: {
			Account *account = budget->expensesAccounts.first();
			while(account) {
				values[account] = 0.0;
				counts[account] = 0.0;
				account = budget->expensesAccounts.next();
			}
			account = budget->incomesAccounts.first();
			while(account) {
				values[account] = 0.0;
				counts[account] = 0.0;
				account = budget->incomesAccounts.next();
			}
			break;
		}
		default: {
			int i = sourceCombo->currentIndex() - 1;
			if(i < (int) budget->expensesAccounts.count()) current_account = budget->expensesAccounts.at(i);
			else current_account = budget->incomesAccounts.at(i - budget->expensesAccounts.count());
			if(current_account) {
				type = current_account->type();
				if(b_extra) {
					if(has_empty_description) descriptionCombo->setItemText(descriptionCombo->count() - 1, "");
					if(has_empty_payee) payeeCombo->setItemText(payeeCombo->count() - 1, "");
					if(descriptionButton->isChecked()) {
						int p_index = payeeCombo->currentIndex();
						if(p_index == 0)  {
							i_source = 1;
						} else {
							current_payee = payeeCombo->itemText(p_index);
							i_source = 3;
						}
					} else {
						int d_index = descriptionCombo->currentIndex();
						if(d_index == 0)  {
							i_source = 2;
						} else {
							current_description = descriptionCombo->itemText(d_index);
							i_source = 4;
						}
					}
				} else {
					i_source = 1;
				}
				Transaction *trans = budget->transactions.first();
				while(trans) {
					if((trans->fromAccount() == current_account || trans->toAccount() == current_account) && (i_source <= 2 || (i_source == 4 && trans->description() == current_description) || (i_source == 3 && ((trans->type() == TRANSACTION_TYPE_EXPENSE && ((Expense*) trans)->payee() == current_payee) || (trans->type() == TRANSACTION_TYPE_INCOME && ((Income*) trans)->payer() == current_payee))))) {
						if(i_source == 2 || i_source == 4) {
							if(trans->type() == TRANSACTION_TYPE_EXPENSE) {
								desc_values[((Expense*) trans)->payee()] = 0.0;
								desc_counts[((Expense*) trans)->payee()] = 0.0;
							} else if(trans->type() == TRANSACTION_TYPE_INCOME) {
								desc_values[((Income*) trans)->payer()] = 0.0;
								desc_counts[((Income*) trans)->payer()] = 0.0;
							}
						} else {
							desc_values[trans->description()] = 0.0;
							desc_counts[trans->description()] = 0.0;
						}
					}
					trans = budget->transactions.next();
				}
			}
		}
	}

	QDate first_date;
	if(fromButton->isChecked()) {
		first_date = from_date;
	} else {
		Transaction *trans = budget->transactions.first();
		while(trans) {
			if(trans->fromAccount()->type() != ACCOUNT_TYPE_ASSETS || trans->toAccount()->type() != ACCOUNT_TYPE_ASSETS) {
				first_date = trans->date();
				break;
			}
			trans = budget->transactions.next();
		}
		if(first_date.isNull()) first_date = QDate::currentDate();
		if(first_date > to_date) first_date = to_date;
	}
	Transaction *trans = budget->transactions.first();
	bool first_date_reached = false;
	while(trans) {
		if(!first_date_reached && trans->date() >= first_date) first_date_reached = true;
		else if(first_date_reached && trans->date() > to_date) break;
		if(first_date_reached) {
			if(current_account) {
				int sign = 1;
				bool include = false;
				if(trans->fromAccount() == current_account && (i_source <= 2 || (i_source == 4 && trans->description() == current_description) || (i_source == 3 && ((trans->type() == TRANSACTION_TYPE_EXPENSE && ((Expense*) trans)->payee() == current_payee) || (trans->type() == TRANSACTION_TYPE_INCOME && ((Income*) trans)->payer() == current_payee))))) {
					include = true;
					if(type == ACCOUNT_TYPE_INCOMES) sign = 1;
					else sign = -1;
				} else if(trans->toAccount() == current_account && (i_source <= 2 || (i_source == 4 && trans->description() == current_description) || (i_source == 3 && ((trans->type() == TRANSACTION_TYPE_EXPENSE && ((Expense*) trans)->payee() == current_payee) || (trans->type() == TRANSACTION_TYPE_INCOME && ((Income*) trans)->payer() == current_payee))))) {
					include = true;
					if(type == ACCOUNT_TYPE_EXPENSES) sign = 1;
					else sign = -1;
				}
				if(include) {
					if(i_source == 2 || i_source == 4) {
						if(trans->type() == TRANSACTION_TYPE_EXPENSE) {
							desc_values[((Expense*) trans)->payee()] += trans->value() * sign;
							value += trans->value() * sign;
							desc_counts[((Expense*) trans)->payee()] += trans->quantity();
							value_count += trans->quantity();
						} else if(trans->type() == TRANSACTION_TYPE_INCOME) {
							desc_values[((Income*) trans)->payer()] += trans->value() * sign;
							value += trans->value() * sign;
							desc_counts[((Income*) trans)->payer()] += trans->quantity();
							value_count += trans->quantity();
						}
					} else {
						desc_values[trans->description()] += trans->value() * sign;
						value += trans->value() * sign;
						desc_counts[trans->description()] += trans->quantity();
						value_count += trans->quantity();
					}
				}
			} else {
				if(trans->fromAccount()->type() == ACCOUNT_TYPE_EXPENSES) {
					values[trans->fromAccount()] -= trans->value();
					costs -= trans->value();
					counts[trans->fromAccount()] += trans->quantity();
					costs_count += trans->quantity();
				} else if(trans->fromAccount()->type() == ACCOUNT_TYPE_INCOMES) {
					values[trans->fromAccount()] += trans->value();
					incomes += trans->value();
					counts[trans->fromAccount()] += trans->quantity();
					incomes_count += trans->quantity();
				} else if(trans->toAccount()->type() == ACCOUNT_TYPE_EXPENSES) {
					values[trans->toAccount()] += trans->value();
					costs += trans->value();
					counts[trans->toAccount()] += trans->quantity();
					costs_count += trans->quantity();
				} else if(trans->toAccount()->type() == ACCOUNT_TYPE_INCOMES) {
					values[trans->toAccount()] -= trans->value();
					incomes -= trans->value();
					counts[trans->toAccount()] += trans->quantity();
					incomes_count += trans->quantity();
				}
			}
		}
		trans = budget->transactions.next();
	}
	first_date_reached = false;
	ScheduledTransaction *strans = budget->scheduledTransactions.first();
	while(strans) {
		trans = strans->transaction();
		if(!first_date_reached && trans->date() >= first_date) first_date_reached = true;
		else if(first_date_reached && trans->date() > to_date) break;
		if(first_date_reached) {
			if(current_account) {
				int sign = 1;
				bool include = false;
				if(trans->fromAccount() == current_account && (i_source <= 2 || (i_source == 4 && trans->description() == current_description) || (i_source == 3 && ((trans->type() == TRANSACTION_TYPE_EXPENSE && ((Expense*) trans)->payee() == current_payee) || (trans->type() == TRANSACTION_TYPE_INCOME && ((Income*) trans)->payer() == current_payee))))) {
					include = true;
					if(type == ACCOUNT_TYPE_INCOMES) sign = 1;
					else sign = -1;
				} else if(trans->toAccount() == current_account && (i_source <= 2 || (i_source == 4 && trans->description() == current_description) || (i_source == 3 && ((trans->type() == TRANSACTION_TYPE_EXPENSE && ((Expense*) trans)->payee() == current_payee) || (trans->type() == TRANSACTION_TYPE_INCOME && ((Income*) trans)->payer() == current_payee))))) {
					include = true;
					if(type == ACCOUNT_TYPE_EXPENSES) sign = 1;
					else sign = -1;
				}
				if(include) {
					int count = strans->recurrence() ? strans->recurrence()->countOccurrences(first_date, to_date) : 1;
					if(i_source == 2 || i_source == 4) {
						if(trans->type() == TRANSACTION_TYPE_EXPENSE) {
							desc_values[((Expense*) trans)->payee()] += trans->value() * count * sign;
							value += trans->value() * count * sign;
							desc_counts[((Expense*) trans)->payee()] += count * trans->quantity();
							value_count += count * trans->quantity();
						} else if(trans->type() == TRANSACTION_TYPE_INCOME) {
							desc_values[((Income*) trans)->payer()] += trans->value() * count * sign;
							value += trans->value() * count * sign;
							desc_counts[((Income*) trans)->payer()] += count * trans->quantity();
							value_count += count * trans->quantity();
						}
					} else {
						desc_values[trans->description()] += trans->value() * count * sign;
						value += trans->value() * count * sign;
						desc_counts[trans->description()] += count * trans->quantity();
						value_count += count * trans->quantity();
					}
				}
			} else {
				if(trans->fromAccount()->type() == ACCOUNT_TYPE_EXPENSES) {
					int count = strans->recurrence() ? strans->recurrence()->countOccurrences(first_date, to_date) : 1;
					counts[trans->fromAccount()] += count * trans->quantity();
					values[trans->fromAccount()] -= trans->value() * count;
					costs_count += count * trans->quantity();
					costs -= trans->value() * count;
				} else if(trans->fromAccount()->type() == ACCOUNT_TYPE_INCOMES) {
					int count = strans->recurrence() ? strans->recurrence()->countOccurrences(first_date, to_date) : 1;
					counts[trans->fromAccount()] += count * trans->quantity();
					values[trans->fromAccount()] += trans->value() * count;
					incomes_count += count * trans->quantity();
					incomes += trans->value() * count;
				} else if(trans->toAccount()->type() == ACCOUNT_TYPE_EXPENSES) {
					int count = strans->recurrence() ? strans->recurrence()->countOccurrences(first_date, to_date) : 1;
					counts[trans->toAccount()] += count * trans->quantity();
					values[trans->toAccount()] += trans->value() * count;
					costs_count += count * trans->quantity();
					costs += trans->value() * count;
				} else if(trans->toAccount()->type() == ACCOUNT_TYPE_INCOMES) {
					int count = strans->recurrence() ? strans->recurrence()->countOccurrences(first_date, to_date) : 1;
					counts[trans->toAccount()] += count * trans->quantity();
					values[trans->toAccount()] -= trans->value() * count;
					incomes_count += count * trans->quantity();
					incomes -= trans->value() * count;
				}
			}
		}
		strans = budget->scheduledTransactions.next();
	}
	source = "";
	QString title;
	if(current_account && type == ACCOUNT_TYPE_EXPENSES) {
		if(i_source == 4) title = tr("Expenses: %2, %1").arg(current_account->name()).arg(current_description.isEmpty() ? tr("No description") : current_description);
		else if(i_source == 3) title = tr("Expenses: %2, %1").arg(current_account->name()).arg(current_payee.isEmpty() ? tr("No payee") : current_payee);
		else title = tr("Expenses: %1").arg(current_account->name());
	} else if(current_account && type == ACCOUNT_TYPE_INCOMES) {
		if(i_source == 4) title = tr("Incomes: %2, %1").arg(current_account->name()).arg(current_description.isEmpty() ? tr("No description") : current_description);
		else if(i_source == 3) title = tr("Incomes: %2, %1").arg(current_account->name()).arg(current_payee.isEmpty() ? tr("No payer") : current_payee);
		else title = tr("Incomes: %1").arg(current_account->name());
	} else {
		title = tr("Incomes & Expenses");
	}
	QTextStream outf(&source, QIODevice::WriteOnly);
	outf << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">" << '\n';
	outf << "<html>" << '\n';
	outf << "\t<head>" << '\n';
	outf << "\t\t<title>";
	outf << htmlize_string(title);
	outf << "</title>" << '\n';
	outf << "\t\t<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">" << '\n';
	outf << "\t\t<meta name=\"GENERATOR\" content=\"" << qApp->applicationDisplayName() << "\">" << '\n';
	outf << "\t</head>" << '\n';
	outf << "\t<body>" << '\n';
	if(fromButton->isChecked()) outf << "\t\t<h2>" << tr("%1 (%2&ndash;%3)", "html format; %1: title; %2: from date; %3: to date").arg(htmlize_string(title)).arg(htmlize_string(QLocale().toString(first_date, QLocale::ShortFormat))).arg(htmlize_string(QLocale().toString(to_date, QLocale::ShortFormat))) << "</h2>" << '\n';
	else outf << "\t\t<h2>" << tr("%1 (to %2)", "html format; %1: title; %2: to date").arg(htmlize_string(title)).arg(htmlize_string(QLocale().toString(to_date, QLocale::ShortFormat))) << "</h2>" << '\n';
	outf << "\t\t<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"5\">" << '\n';
	outf << "\t\t\t<thead align=\"left\">" << '\n';
	outf << "\t\t\t\t<tr>" << '\n';
	outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Category")) << "</th>";
	if(current_account && type == ACCOUNT_TYPE_EXPENSES) {
		if(enabled[0]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Cost")) << "</th>";
	} else if(current_account && type == ACCOUNT_TYPE_INCOMES) {
		if(enabled[0]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Income")) << "</th>";
	} else {
		if(enabled[0]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Value")) << "</th>";
	}
	if(enabled[1]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Daily Average")) << "</th>";
	if(enabled[2]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Monthly Average")) << "</th>";
	if(enabled[3]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Yearly Average")) << "</th>";
	if(enabled[4]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Quantity")) << "</th>";
	if(current_account && type == ACCOUNT_TYPE_EXPENSES) {
		if(enabled[5]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Average Cost")) << "</th>";
	} else if(current_account && type == ACCOUNT_TYPE_INCOMES) {
		if(enabled[5]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Average Income")) << "</th>";
	} else {
		if(enabled[5]) outf << "\t\t\t\t\t<th style=\"border-bottom: thin solid\">" << htmlize_string(tr("Average Value")) << "</th>";
	}
	outf << "\t\t\t\t</tr>" << '\n';
	outf << "\t\t\t</thead>" << '\n';
	outf << "\t\t\t<tbody>" << '\n';
	int days = first_date.daysTo(to_date) + 1;
	double months = monthsBetweenDates(first_date, to_date), years = yearsBetweenDates(first_date, to_date);
	int i_count_frac = 0;
	double intpart = 0.0;
	if(current_account) {
		QMap<QString, double>::iterator it_e = desc_counts.end();
		for(QMap<QString, double>::iterator it = desc_counts.begin(); it != it_e; ++it) {
			if(modf(it.value(), &intpart) != 0.0) {
				i_count_frac = 2;
				break;
			}
		}
	} else {
		Account *account = budget->incomesAccounts.first();
		while(account) {
			if(modf(counts[account], &intpart) != 0.0) {
				i_count_frac = 2;
				break;
			}
			account = budget->incomesAccounts.next();
		}
		if(i_count_frac == 0) {
			account = budget->expensesAccounts.first();
			while(account) {
				if(modf(counts[account], &intpart) != 0.0) {
					i_count_frac = 2;
					break;
				}
				account = budget->expensesAccounts.next();
			}
		}
	}
	if(current_account) {
		QMap<QString, double>::iterator it_e = desc_values.end();
		QMap<QString, double>::iterator itc = desc_counts.begin();
		QMap<QString, double>::iterator it = desc_values.begin();
		for(; it != it_e; ++it, ++itc) {
			outf << "\t\t\t\t<tr>" << '\n';
			if(it.key().isEmpty()) {
				if((i_source == 4 || i_source == 2) && type == ACCOUNT_TYPE_EXPENSES) outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(tr("No payee")) << "</td>";
				else if(i_source == 4 || i_source == 2) outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(tr("No payer")) << "</td>";
				else outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(tr("No description")) << "</td>";
			} else {
				outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(it.key()) << "</td>";
			}
			if(enabled[0]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(it.value())) << "</td>";
			if(enabled[1]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(it.value() / days)) << "</td>";
			if(enabled[2]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(it.value() / months)) << "</td>";
			if(enabled[3]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(it.value() / years)) << "</td>";
			if(enabled[4]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toString(itc.value(), 'f', i_count_frac)) << "</td>";
			if(enabled[5]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(itc.value() == 0.0 ? 0.0 : (it.value() / itc.value()))) << "</td>";
			outf << "\n";
			outf << "\t\t\t\t</tr>" << '\n';
		}
		outf << "\t\t\t\t<tr>" << '\n';
		outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid; border-top: thin solid\"><b>" << htmlize_string(tr("Total")) << "</b></td>";
		if(enabled[0]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toCurrencyString(value)) << "</b></td>";
		if(enabled[1]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toCurrencyString(value / days)) << "</b></td>";
		if(enabled[2]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toCurrencyString(value / months)) << "</b></td>";
		if(enabled[3]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toCurrencyString(value / years)) << "</b></td>";
		if(enabled[4]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toString(value_count, 'f', i_count_frac)) << "</b></td>";
		if(enabled[5]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid;\"><b>" << htmlize_string(QLocale().toCurrencyString(value_count == 0.0 ? 0.0 : (value / value_count))) << "</b></td>";
		outf << "\n";
		outf << "\t\t\t\t</tr>" << '\n';
	} else {
		Account *account = budget->incomesAccounts.first();
		while(account) {
			outf << "\t\t\t\t<tr>" << '\n';
			outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(account->name()) << "</td>";
			if(enabled[0]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(values[account])) << "</td>";
			if(enabled[1]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(values[account] / days)) << "</td>";
			if(enabled[2]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(values[account] / months)) << "</td>";
			if(enabled[3]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(values[account] / years)) << "</td>";
			if(enabled[4]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toString(counts[account], 'f', i_count_frac)) << "</td>";
			if(enabled[5]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(counts[account] == 0.0 ? 0.0 : (values[account] / counts[account]))) << "</td>";
			outf << "\n";
			outf << "\t\t\t\t</tr>" << '\n';
			account = budget->incomesAccounts.next();
		}
		outf << "\t\t\t\t<tr>" << '\n';
		outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid; border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(tr("Total incomes")) << "</b></td>";
		if(enabled[0]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes)) << "</b></td>";
		if(enabled[1]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes / days)) << "</b></td>";
		if(enabled[2]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes / months)) << "</b></td>";
		if(enabled[3]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes / years)) << "</b></td>";
		if(enabled[4]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toString(incomes_count, 'f', i_count_frac)) << "</b></td>";
		if(enabled[5]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes_count == 0.0 ? 0.0 : (incomes / incomes_count))) << "</b></td>";
		outf << "\n";
		outf << "\t\t\t\t</tr>" << '\n';
		account = budget->expensesAccounts.first();
		while(account) {
			outf << "\t\t\t\t<tr>" << '\n';
			outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\">" << htmlize_string(account->name()) << "</td>";
			if(enabled[0]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(-values[account])) << "</td>";
			if(enabled[1]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(-values[account] / days)) << "</td>";
			if(enabled[2]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(-values[account] / months)) << "</td>";
			if(enabled[3]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(-values[account] / years)) << "</td>";
			if(enabled[4]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toString(counts[account], 'f', i_count_frac)) << "</td>";
			if(enabled[5]) outf << "<td nowrap align=\"right\">" << htmlize_string(QLocale().toCurrencyString(counts[account] == 0.0 ? 0.0 : (-values[account] / counts[account]))) << "</td>";
			outf << "\n";
			outf << "\t\t\t\t</tr>" << '\n';
			account = budget->expensesAccounts.next();
		}
		outf << "\t\t\t\t<tr>" << '\n';
		outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid; border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(tr("Total expenses")) << "</b></td>";
		if(enabled[0]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(-costs)) << "</b></td>";
		if(enabled[1]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(-costs / days)) << "</b></td>";
		if(enabled[2]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(-costs / months)) << "</b></td>";
		if(enabled[3]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(-costs / years)) << "</b></td>";
		if(enabled[4]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toString(costs_count, 'f', i_count_frac)) << "</b></td>";
		if(enabled[5]) outf << "<td nowrap align=\"right\" style=\"border-top: thin solid; border-bottom: thin solid\"><b>" << htmlize_string(QLocale().toCurrencyString(costs_count == 0.0 ? 0.0 : (-costs / costs_count))) << "</b></td>";
		outf << "\n";
		outf << "\t\t\t\t</tr>" << '\n';
		outf << "\t\t\t\t<tr>" << '\n';
		outf << "\t\t\t\t\t<td align=\"left\" style=\"border-right: thin solid\"><b>" << htmlize_string(tr("Total (Profits)")) << "</b></td>";
		if(enabled[0]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toCurrencyString(incomes - costs)) << "</b></td>";
		if(enabled[1]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toCurrencyString((incomes - costs) / days)) << "</b></td>";
		if(enabled[2]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toCurrencyString((incomes - costs) / months)) << "</b></td>";
		if(enabled[3]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toCurrencyString((incomes - costs) / years)) << "</b></td>";
		if(enabled[4]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toString(incomes_count + costs_count, 'f', i_count_frac)) << "</b></td>";
		if(enabled[5]) outf << "<td nowrap align=\"right\"><b>" << htmlize_string(QLocale().toCurrencyString((incomes_count + costs_count) == 0.0 ? 0.0 : ((incomes - costs) / (incomes_count + costs_count)))) << "</b></td>";
		outf << "\n";
		outf << "\t\t\t\t</tr>" << '\n';
	}
	outf << "\t\t\t</tbody>" << '\n';
	outf << "\t\t</table>" << '\n';
	outf << "\t</body>" << '\n';
	outf << "</html>" << '\n';
	htmlview->setHtml(source);
	if(current_account && b_extra) {
		if(has_empty_description) descriptionCombo->setItemText(descriptionCombo->count() - 1, tr("No description"));
		if(has_empty_payee) {
			if(current_account->type() == ACCOUNT_TYPE_EXPENSES) payeeCombo->setItemText(payeeCombo->count() - 1, tr("No payee"));
			else payeeCombo->setItemText(payeeCombo->count() - 1, tr("No payer"));
		}
	}
}

void CategoriesComparisonReport::updateTransactions() {
	if(b_extra && current_account) {
		int curindex_d = 0, curindex_p = 0;
		bool restore_d = (descriptionCombo->currentIndex() > 0);
		bool restore_p = (payeeCombo->currentIndex() > 0);
		payeeCombo->blockSignals(true);
		descriptionCombo->blockSignals(true);
		payeeCombo->clear();
		descriptionCombo->clear();
		descriptionCombo->addItem(tr("All descriptions"));
		if(current_account->type() == ACCOUNT_TYPE_EXPENSES) payeeCombo->addItem(tr("All payees"));
		else payeeCombo->addItem(tr("All payers"));
		has_empty_description = false;
		has_empty_payee = false;
		QMap<QString, bool> descriptions, payees;
		Transaction *trans = budget->transactions.first();
		while(trans) {
			if((trans->fromAccount() == current_account || trans->toAccount() == current_account)) {
				if(trans->description().isEmpty()) has_empty_description = true;
				else descriptions[trans->description()] = true;
				if(trans->type() == TRANSACTION_TYPE_EXPENSE) {
					if(((Expense*) trans)->payee().isEmpty()) has_empty_payee = true;
					else payees[((Expense*) trans)->payee()] = true;
				} else if(trans->type() == TRANSACTION_TYPE_INCOME) {
					if(((Income*) trans)->payer().isEmpty()) has_empty_payee = true;
					else payees[((Income*) trans)->payer()] = true;
				}
			}
			trans = budget->transactions.next();
		}
		int i = 1;
		QMap<QString, bool>::iterator it_e = descriptions.end();
		for(QMap<QString, bool>::iterator it = descriptions.begin(); it != it_e; ++it) {
			if(restore_d && it.key() == current_description) {curindex_d = i;}
			descriptionCombo->addItem(it.key());
			i++;
		}
		if(has_empty_description) {
			if(restore_d && current_description.isEmpty()) curindex_d = i;
			descriptionCombo->addItem(tr("No description"));
		}
		if(curindex_d < descriptionCombo->count()) {
			descriptionCombo->setCurrentIndex(curindex_d);
		}
		if(descriptionCombo->currentIndex() == 0) {
			current_description = "";
		}
		i = 1;
		QMap<QString, bool>::iterator it2_e = payees.end();
		for(QMap<QString, bool>::iterator it2 = payees.begin(); it2 != it2_e; ++it2) {
			if(restore_p && it2.key() == current_payee) {curindex_p = i;}
			payeeCombo->addItem(it2.key());
			i++;
		}
		if(has_empty_payee) {
			if(restore_p && current_payee.isEmpty()) curindex_p = i;
			if(current_account->type() == ACCOUNT_TYPE_EXPENSES) payeeCombo->addItem(tr("No payee"));
			else payeeCombo->addItem(tr("No payer"));
		}
		if(curindex_p < payeeCombo->count()) {
			payeeCombo->setCurrentIndex(curindex_d);
		}
		if(payeeCombo->currentIndex() == 0) {
			current_payee = "";
		}
		payeeCombo->blockSignals(false);
		descriptionCombo->blockSignals(false);
	}
	updateDisplay();
}
void CategoriesComparisonReport::updateAccounts() {
	int curindex = 0;
	sourceCombo->blockSignals(true);
	sourceCombo->clear();
	sourceCombo->addItem(tr("All Categories"));
	int i = 1;
	Account *account = budget->expensesAccounts.first();
	while(account) {
		sourceCombo->addItem(tr("Expenses: %1").arg(account->name()));
		if(account == current_account) curindex = i;
		account = budget->expensesAccounts.next();
		i++;
	}
	account = budget->incomesAccounts.first();
	while(account) {
		sourceCombo->addItem(tr("Incomes: %1").arg(account->name()));
		if(account == current_account) curindex = i;
		account = budget->incomesAccounts.next();
		i++;
	}
	if(curindex < sourceCombo->count()) sourceCombo->setCurrentIndex(curindex);
	sourceCombo->blockSignals(false);
	if(curindex == 0 && b_extra) {
		sourceChanged(curindex);
	} else {
		updateDisplay();
	}
}

