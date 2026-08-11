const buttons = document.querySelectorAll('.choose');
const selection = document.querySelector('#selection');

buttons.forEach((button) => {
  button.addEventListener('click', () => {
    buttons.forEach((item) => item.classList.remove('is-selected'));
    button.classList.add('is-selected');
    selection.textContent = `Selected: ${button.dataset.choice}`;
    selection.classList.add('has-choice');
    localStorage.setItem('wine4office-design-choice', button.dataset.choice);
  });
});

const savedChoice = localStorage.getItem('wine4office-design-choice');
if (savedChoice) {
  const savedButton = [...buttons].find((button) => button.dataset.choice === savedChoice);
  if (savedButton) savedButton.click();
}
