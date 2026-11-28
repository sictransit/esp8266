import { Component } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { Header } from './components/header/header';
import { Main } from './components/main/main';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, Header, Main],
  templateUrl: './app.html',
  styleUrl: './app.scss'
})
export class App {
  protected title = 'mosquito';
}
